#pragma once
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Composition.h>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace DockPhysics {

    struct DockItem {
        winrt::Windows::UI::Composition::Visual visual{ nullptr };
        float baseX = 0.0f; // Initial screen X coordinate of center
        float targetScale = 1.0f;
        float currentScale = 1.0f;
        float velocityScale = 0.0f;
        
        float targetOffset = 0.0f;
        float currentOffset = 0.0f;
        float velocityOffset = 0.0f;

        float targetOffsetY = 0.0f;
        float currentOffsetY = 0.0f;
        float velocityOffsetY = 0.0f;
    };

    class Engine {
    public:
        Engine();
        ~Engine();

        // Add a visual to be managed by the physics engine
        void AddItem(winrt::Windows::UI::Composition::Visual visual, float centerX);
        void ClearItems();
        
        // Update the mouse position (e.g. from a low-level hook)
        // This wakes the physics thread if it's sleeping.
        void UpdateMousePosition(float mouseX, bool isMouseOverTaskbar);
        
        // Trigger upward bounce impulse for notification
        void TriggerIconBounce(int iconIndex, float impulseStrength = 800.0f);
        
        void Start();
        void Stop();

    private:
        void UpdateLoop();
        
        std::vector<DockItem> m_items;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        
        float m_mouseX = 0.0f;
        bool m_isMouseOver = false;
        bool m_isSettled = true;
        
        std::atomic<bool> m_running = false;
        std::thread m_thread;
        
        // Physics constants for 2nd-order critically damped harmonic oscillator
        const float k_spring = 200.0f;  // Spring constant
        const float c_damping = 28.28f; // Damping coefficient (2 * sqrt(k_spring)) for critical damping
        
        const float S_max = 1.5f;       // Max magnification scale
        const float sigma = 80.0f;      // Gaussian spread standard deviation
        const float baseWidth = 44.0f;  // Default icon width for displacement calculation
    };

}
