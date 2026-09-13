#include "DockPhysics.h"
#include <cmath>

#ifndef TE_HOVER_HEADROOM_BASE_PX
#define TE_HOVER_HEADROOM_BASE_PX 1024
#endif

using namespace winrt::Windows::Foundation::Numerics;

namespace DockPhysics {

    Engine::Engine() {}
    
    Engine::~Engine() { 
        Stop(); 
    }

    void Engine::AddItem(winrt::Windows::UI::Composition::Visual visual, float centerX) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.push_back({ visual, centerX, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
        m_isSettled = false;
        m_cv.notify_one();
    }

    void Engine::ClearItems() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.clear();
    }

    void Engine::UpdateMousePosition(float mouseX, bool isMouseOverTaskbar) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_mouseX = mouseX;
            m_isMouseOver = isMouseOverTaskbar;
            m_isSettled = false; // Wake up physics
        }
        m_cv.notify_one();
    }

    void Engine::TriggerIconBounce(int iconIndex, float impulseStrength) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (iconIndex < 0 || iconIndex >= static_cast<int>(m_items.size())) {
                return;
            }
            // Apply massive negative instantaneous velocity (upward impulse)
            m_items[iconIndex].velocityOffsetY = -std::abs(impulseStrength);
            m_isSettled = false;
        }
        m_cv.notify_one();
    }

    void Engine::Start() {
        if (m_running) return;
        m_running = true;
        m_thread = std::thread(&Engine::UpdateLoop, this);
    }

    void Engine::Stop() {
        if (!m_running) return;
        m_running = false;
        m_cv.notify_all();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void Engine::UpdateLoop() {
        auto lastTime = std::chrono::high_resolution_clock::now();
        
        while (m_running) {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                
                // If settled and mouse is not triggering changes, sleep to drop CPU usage to 0.0%
                if (m_isSettled && !m_isMouseOver) {
                    m_cv.wait(lock, [this] { return !m_isSettled || !m_running; });
                    if (!m_running) break;
                    // Reset time after waking up to avoid huge dt jump
                    lastTime = std::chrono::high_resolution_clock::now();
                }
                
                auto currentTime = std::chrono::high_resolution_clock::now();
                float dt = std::chrono::duration<float>(currentTime - lastTime).count();
                lastTime = currentTime;
                
                // Clamp dt for stability (max 50ms)
                if (dt > 0.05f) dt = 0.05f;
                if (dt <= 0.0f) dt = 0.001f;
                
                bool allSettled = true;
                
                // 1. Calculate Targets
                for (auto& item : m_items) {
                    if (m_isMouseOver) {
                        float dist = m_mouseX - item.baseX;
                        // Gaussian magnification curve
                        item.targetScale = 1.0f + (S_max - 1.0f) * std::exp(-(dist * dist) / (2.0f * sigma * sigma));
                    } else {
                        item.targetScale = 1.0f;
                    }
                }
                
                // 2. Calculate continuous horizontal neighbor displacement
                for (size_t i = 0; i < m_items.size(); ++i) {
                    float shift = 0.0f;
                    for (size_t j = 0; j < m_items.size(); ++j) {
                        if (i == j) continue;
                        float influence = (m_items[j].targetScale - 1.0f) * baseWidth;
                        // Push elements away
                        if (j < i) shift += influence / 2.0f;
                        else       shift -= influence / 2.0f;
                    }
                    m_items[i].targetOffset = shift;
                }
                
                // 3. Apply 2nd-order critically damped harmonic oscillator physics
                for (auto& item : m_items) {
                    // Scale update
                    float forceScale = -k_spring * (item.currentScale - item.targetScale) - c_damping * item.velocityScale;
                    item.velocityScale += forceScale * dt;
                    item.currentScale += item.velocityScale * dt;
                    
                    // Offset update
                    float forceOffset = -k_spring * (item.currentOffset - item.targetOffset) - c_damping * item.velocityOffset;
                    item.velocityOffset += forceOffset * dt;
                    item.currentOffset += item.velocityOffset * dt;

                    // Vertical bounce offset update (targetOffsetY naturally rests at 0.0f)
                    float forceOffsetY = -k_spring * (item.currentOffsetY - item.targetOffsetY) - c_damping * item.velocityOffsetY;
                    item.velocityOffsetY += forceOffsetY * dt;
                    item.currentOffsetY += item.velocityOffsetY * dt;

                    // Honor headroom constraint so the icon doesn't get clipped at the top of the bounce
                    if (item.currentOffsetY < -static_cast<float>(TE_HOVER_HEADROOM_BASE_PX)) {
                        item.currentOffsetY = -static_cast<float>(TE_HOVER_HEADROOM_BASE_PX);
                        if (item.velocityOffsetY < 0.0f) {
                            item.velocityOffsetY = 0.0f;
                        }
                    }
                    // Prevent dipping below taskbar resting baseline
                    if (item.currentOffsetY > 0.0f) {
                        item.currentOffsetY = 0.0f;
                        item.velocityOffsetY = 0.0f;
                    }
                    
                    // Check settlement threshold
                    if (std::abs(item.currentScale - item.targetScale) > 0.005f || std::abs(item.velocityScale) > 0.01f ||
                        std::abs(item.currentOffset - item.targetOffset) > 0.5f || std::abs(item.velocityOffset) > 1.0f ||
                        std::abs(item.currentOffsetY - item.targetOffsetY) > 0.5f || std::abs(item.velocityOffsetY) > 1.0f) {
                        allSettled = false;
                    } else if (m_isSettled) {
                        // Snap exactly when settling
                        item.currentScale = item.targetScale;
                        item.currentOffset = item.targetOffset;
                        item.currentOffsetY = item.targetOffsetY;
                        item.velocityScale = 0.0f;
                        item.velocityOffset = 0.0f;
                        item.velocityOffsetY = 0.0f;
                    }
                    
                    // Directly apply to Composition Visual, bypassing XAML layout passes
                    if (item.visual) {
                        item.visual.Scale(float3(item.currentScale, item.currentScale, 1.0f));
                        item.visual.Offset(float3(item.currentOffset, item.currentOffsetY, 0.0f));
                    }
                }
                
                m_isSettled = allSettled;
            }
            
            // Sleep for ~144Hz target frame rate (~6.94ms)
            if (!m_isSettled || m_isMouseOver) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

}
