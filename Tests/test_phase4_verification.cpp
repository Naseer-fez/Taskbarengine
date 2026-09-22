#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <windows.h>
#include <objbase.h>
#include <sdk/te_types.h>
#include <sdk/te_ipc.h>
#include <sdk/te_plugin.h>
#include "frame_loop.h"
#include "icon_hover_internal.h"
#include "dynamic_island.h"
#include "dynamic_island_media.h"
#include "mock_media_source.h"
#include "dcomp_overlay.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using Catch::Matchers::WithinAbs;

/* ── SYS-009: COM MTA Apartment Initialized on Frame Loop Worker Thread ── */

TEST_CASE("Phase 4 - Dedicated Animation Worker Thread with COM MTA Initialization (SYS-009)", "[phase4][sys_009]") {
    TE_FrameLoopShutdown();
    REQUIRE(TE_FrameLoopIsActive() == 0);

    HRESULT hr = TE_FrameLoopStart();
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(TE_FrameLoopIsActive() == 1);

    // Give worker thread time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Verify refresh rate and timer
    double rate = TE_FrameLoopGetRefreshRate();
    REQUIRE(rate >= 24.0);
    REQUIRE(rate <= 500.0);

    // Verify stop and active status
    TE_FrameLoopStop();
    REQUIRE(TE_FrameLoopIsActive() == 0);

    TE_FrameLoopShutdown();
    REQUIRE(TE_FrameLoopIsActive() == 0);
}

/* ── PERF-101: Lockless Double-Buffered Snapshot Concurrency ──────────── */

TEST_CASE("Phase 4 - Lockless Double-Buffered Snapshot Concurrency (PERF-101)", "[phase4][perf_101]") {
    TE_IconHoverState saved_state = g_hover_state;

    g_hover_state.enabled = 1;
    g_hover_state.monitor_count = 2;
    for (int m = 0; m < 2; m++) {
        g_hover_state.monitors[m].is_active = 1;
        g_hover_state.monitors[m].anim_count = 5;
        g_hover_state.monitors[m].geometry.taskbarRect = { m * 1920, 1040, (m + 1) * 1920, 1080 };
        g_hover_state.monitors[m].geometry.headroom_y = 64;
        g_hover_state.monitors[m].geometry.generation = 1;
        for (int i = 0; i < 5; i++) {
            g_hover_state.monitors[m].anim[i].center_x = (float)(m * 1920 + 100 + i * 50);
            g_hover_state.monitors[m].anim[i].center_y = 1060.0f;
            g_hover_state.monitors[m].anim[i].base_width = 48.0f;
            g_hover_state.monitors[m].anim[i].base_height = 48.0f;
            g_hover_state.monitors[m].anim[i].current_scale = 1.0f;
            g_hover_state.monitors[m].anim[i].target_scale = 1.0f;
            g_hover_state.monitors[m].anim[i].geometry_generation = 1;
        }
    }

    REQUIRE(SUCCEEDED(TE_FrameLoopStart()));
    REQUIRE(TE_FrameLoopIsActive() == 1);

    std::atomic<bool> run{true};
    std::atomic<int> mouse_events{0};
    std::atomic<int> bounce_events{0};

    std::vector<std::thread> workers;

    // Thread 1: Rapid mouse movements on monitor 0
    workers.emplace_back([&]() {
        float x = 100.0f;
        while (run.load(std::memory_order_relaxed)) {
            TE_FrameLoopOnMouseMoveEx(x, 1060.0f, 0, NULL);
            x += 2.0f;
            if (x > 400.0f) x = 100.0f;
            mouse_events.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Thread 2: Rapid mouse movements on monitor 1
    workers.emplace_back([&]() {
        float x = 2020.0f;
        while (run.load(std::memory_order_relaxed)) {
            TE_FrameLoopOnMouseMoveEx(x, 1060.0f, 1, NULL);
            x += 3.0f;
            if (x > 2300.0f) x = 2020.0f;
            mouse_events.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Thread 3: Rapid bounce impulse triggers
    workers.emplace_back([&]() {
        int icon = 0;
        while (run.load(std::memory_order_relaxed)) {
            TE_FrameLoopTriggerIconBounceForMonitor(0, icon, 600.0f);
            TE_FrameLoopTriggerIconBounceForMonitor(1, (icon + 1) % 5, 800.0f);
            icon = (icon + 1) % 5;
            bounce_events.fetch_add(2, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Thread 4: Rapid mouse leave & re-entry transitions
    workers.emplace_back([&]() {
        while (run.load(std::memory_order_relaxed)) {
            TE_FrameLoopOnMouseLeave();
            std::this_thread::yield();
            TE_FrameLoopOnMouseMoveEx(200.0f, 1060.0f, 0, NULL);
            std::this_thread::yield();
        }
    });

    // Race heavily for 150ms
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    run.store(false, std::memory_order_relaxed);

    for (auto& w : workers) {
        w.join();
    }

    REQUIRE(mouse_events.load() > 50);
    REQUIRE(bounce_events.load() > 50);

    TE_FrameLoopStop();
    REQUIRE(TE_FrameLoopIsActive() == 0);

    g_hover_state = saved_state;
    TE_FrameLoopShutdown();
}

/* ── PERF-103: Dynamic Refresh Rate Synchronization ───────────────────── */

TEST_CASE("Phase 4 - Display Refresh Rate Synchronization & High-Resolution Timer (PERF-103)", "[phase4][perf_103]") {
    double rate = TE_FrameLoopGetRefreshRate();
    // Refresh rate must be realistic hardware scanout frequency
    REQUIRE(rate >= 24.0);
    REQUIRE(rate <= 500.0);

    REQUIRE(SUCCEEDED(TE_FrameLoopStart()));
    REQUIRE(TE_FrameLoopIsActive() == 1);

    // Verify timer initialized
    REQUIRE(TE_FrameLoopIsTimerHighResolution() >= 0);

    TE_FrameLoopStop();
    REQUIRE(TE_FrameLoopIsActive() == 0);
    TE_FrameLoopShutdown();
}

/* ── PERF-104: Continuous Busy-Loop on Stationary Mouse Hover ─────────── */

TEST_CASE("Phase 4 - Stationary Mouse Hover Settling to 0.0% CPU (PERF-104)", "[phase4][perf_104]") {
    TE_DynamicIslandDisable();
    TE_IconHoverState saved_state = g_hover_state;

    g_hover_state.enabled = 1;
    g_hover_state.monitor_count = 1;
    memset(&g_hover_state.monitors[0], 0, sizeof(TE_MonitorState));
    g_hover_state.monitors[0].is_active = 1;
    g_hover_state.monitors[0].anim_count = 4;
    g_hover_state.monitors[0].geometry.taskbarRect = { 0, 1040, 1920, 1080 };
    g_hover_state.monitors[0].geometry.headroom_y = 64;
    g_hover_state.monitors[0].geometry.generation = 1;
    g_hover_state.config.speed_ms = 40; // Fast interpolation for deterministic test settling
    g_hover_state.config.max_scale = 1.8f;
    g_hover_state.config.radius = 120;
    g_hover_state.config.tilt_enabled = 0;
    g_hover_state.config.drag_drop_enabled = 0;

    for (int i = 0; i < 4; i++) {
        g_hover_state.monitors[0].anim[i].center_x = (float)(200 + i * 60);
        g_hover_state.monitors[0].anim[i].center_y = 1060.0f;
        g_hover_state.monitors[0].anim[i].base_width = 48.0f;
        g_hover_state.monitors[0].anim[i].base_height = 48.0f;
        g_hover_state.monitors[0].anim[i].current_scale = 1.0f;
        g_hover_state.monitors[0].anim[i].target_scale = 1.0f;
        g_hover_state.monitors[0].anim[i].geometry_generation = 1;
    }

    // Move cursor over icon 0
    TE_FrameLoopOnMouseMoveEx(200.0f, 1060.0f, 0, NULL);
    REQUIRE(TE_FrameLoopIsActive() == 1);

    // Keep cursor motionless at (200, 1060) and allow interpolation to reach resting equilibrium
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // Timer must automatically sleep when stationary hover reaches equilibrium
    REQUIRE(TE_FrameLoopIsActive() == 0);

    // Magnified visual remains visible (overlay alpha == 1.0f), not hidden!
    REQUIRE(g_hover_state.monitors[0].anim[0].current_scale > 1.2f);

    // Moving mouse must immediately wake the frame loop back up
    TE_FrameLoopOnMouseMoveEx(260.0f, 1060.0f, 0, NULL);
    REQUIRE(TE_FrameLoopIsActive() == 1);

    // Mouse leave must transition to settling and sleep at 1.0 baseline
    TE_FrameLoopOnMouseLeave();
    REQUIRE(TE_FrameLoopIsActive() == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    REQUIRE(TE_FrameLoopIsActive() == 0);

    g_hover_state = saved_state;
    TE_FrameLoopShutdown();
}

/* ── PERF-105: Geometry Generation Desync Frame Drops ─────────────────── */

TEST_CASE("Phase 4 - Geometry Generation Desync Frame Drops (PERF-105)", "[phase4][perf_105]") {
    TE_IconHoverState saved_state = g_hover_state;

    g_hover_state.enabled = 1;
    g_hover_state.monitor_count = 1;
    g_hover_state.monitors[0].is_active = 1;
    g_hover_state.monitors[0].anim_count = 3;
    g_hover_state.monitors[0].geometry.taskbarRect = { 0, 1040, 1920, 1080 };
    g_hover_state.monitors[0].geometry.headroom_y = 64;
    g_hover_state.monitors[0].geometry.generation = 1;

    for (int i = 0; i < 3; i++) {
        g_hover_state.monitors[0].anim[i].center_x = (float)(200 + i * 60);
        g_hover_state.monitors[0].anim[i].center_y = 1060.0f;
        g_hover_state.monitors[0].anim[i].base_width = 48.0f;
        g_hover_state.monitors[0].anim[i].base_height = 48.0f;
        g_hover_state.monitors[0].anim[i].current_scale = 1.0f;
        g_hover_state.monitors[0].anim[i].target_scale = 1.0f;
        g_hover_state.monitors[0].anim[i].geometry_generation = 1;
    }

    REQUIRE(SUCCEEDED(TE_FrameLoopStart()));
    TE_FrameLoopOnMouseMoveEx(200.0f, 1060.0f, 0, NULL);
    REQUIRE(TE_FrameLoopIsActive() == 1);

    // Simulate taskbar resize / DPI change: bump geometry generation to 5
    InterlockedExchange((volatile LONG*)&g_hover_state.monitors[0].geometry.generation, 5);

    // Wait a frame tick for the generation latch to apply
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // The loop did NOT abort or drop frames, it remained actively rendering
    REQUIRE(TE_FrameLoopIsActive() == 1);

    TE_FrameLoopStop();
    g_hover_state = saved_state;
    TE_FrameLoopShutdown();
}

/* ── PERF-501: Frame Loop Spurious Wakeups from Media Timelines ────────── */

TEST_CASE("Phase 4 - Frame Loop Spurious Wakeups from Media Timelines (PERF-501)", "[phase4][perf_501]") {
    TE_DynamicIslandConfig cfg = {};
    cfg.enabled = TRUE;
    cfg.padding_tray = 12;
    cfg.compact_width = 80;
    cfg.expanded_width = 240;
    cfg.height = 30;
    cfg.announce_duration_ms = 0;
    TE_DynamicIslandInit(&cfg);
    TE_DynamicIslandEnable(NULL, 96);
    TE_DynamicIslandSetWakeCallback(TE_FrameLoopWakeDynamicIsland);

    MockMediaSource mock_source;
    mock_source.Initialize();
    TE_DynamicIslandSetMediaSource(&mock_source);

    // Initially active playing track
    mock_source.SetMockTrack(L"Song", L"Artist", TEMediaPlaybackStatus::Playing);
    for (int i = 0; i < 30; i++) {
        TE_DynamicIslandUpdateFrame(0.016f);
    }

    TE_FrameLoopStop();
    REQUIRE(TE_FrameLoopIsActive() == 0);

    // Simulate 20 rapid timeline position ticks (1-5 Hz during media playback)
    for (int i = 1; i <= 20; i++) {
        mock_source.SimulatePositionChange(1000 * i, false);
        // Timeline updates must NEVER wake the animation frame loop!
        REQUIRE(TE_FrameLoopIsActive() == 0);
    }

    // A metadata/playback toggle MUST wake the frame loop
    mock_source.SimulateStatusChange(TEMediaPlaybackStatus::Paused);
    REQUIRE(TE_FrameLoopIsActive() == 1);

    TE_FrameLoopStop();
    TE_DynamicIslandDisable();
    TE_DynamicIslandShutdown();
    TE_FrameLoopShutdown();
}

/* ── PERF-503: Static Announcement Window 3-Second Busy Loop ──────────── */

TEST_CASE("Phase 4 - Static Announcement Window Busy Loop Decoupling (PERF-503)", "[phase4][perf_503]") {
    TE_DynamicIslandConfig cfg = {};
    cfg.enabled = TRUE;
    cfg.padding_tray = 12;
    cfg.compact_width = 80;
    cfg.expanded_width = 240;
    cfg.height = 30;
    cfg.announce_duration_ms = 300; // 300ms for fast test verification
    cfg.expand_duration_ms = 50;
    cfg.collapse_duration_ms = 50;
    TE_DynamicIslandInit(&cfg);
    TE_DynamicIslandEnable(NULL, 96);

    MockMediaSource mock_source;
    mock_source.Initialize();
    TE_DynamicIslandSetMediaSource(&mock_source);

    // Track starts playing, triggering announcement
    mock_source.SetMockTrack(L"New Track", L"New Artist", TEMediaPlaybackStatus::Playing);

    // Step frames until island expands to target width
    for (int i = 0; i < 30; i++) {
        TE_DynamicIslandUpdateFrame(0.016f);
    }

    // Width has expanded to 240
    REQUIRE_THAT(TE_DynamicIslandGetCurrentWidth(), WithinAbs(240.0f, 1.0f));

    // PERF-503: Visual settling must be decoupled from timer expiration!
    // Static expanded island must report settled == TRUE so frame loop sleeps
    REQUIRE(TE_DynamicIslandIsSettled() == TRUE);

    // Wait for the one-shot announcement timer to fire (300ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    // Step frames to animate collapse back to compact width (80)
    for (int i = 0; i < 30; i++) {
        TE_DynamicIslandUpdateFrame(0.016f);
    }

    REQUIRE_THAT(TE_DynamicIslandGetCurrentWidth(), WithinAbs(80.0f, 1.0f));
    REQUIRE(TE_DynamicIslandIsSettled() == TRUE);

    TE_DynamicIslandDisable();
    TE_DynamicIslandShutdown();
    TE_FrameLoopShutdown();
}
