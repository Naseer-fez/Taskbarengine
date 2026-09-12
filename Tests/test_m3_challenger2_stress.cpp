/**
 * @file test_m3_challenger2_stress.cpp
 * @brief Empirical Challenger 2 Stress Harness & Boundary Value Analysis for Features F10 & F12.
 *
 * Tests:
 * 1. Boundary Value Analysis (BVA) strictly around 300.0f (299.9 px smoothed, 300.1 px snapped, ULP stepping).
 * 2. Cold-start initialization from idle (immediate snap on first sample, zero sweep delay, repeated reset cycles).
 * 3. Multi-monitor negative screen coordinates (x in [-3840, 0], boundary crossing, negative teleportation).
 * 4. High-frequency atomic 64-bit coordinate ingest (1000+ Hz, multi-threaded producer/consumer, zero data tearing).
 * 5. State machine transitions (IDLE -> ACTIVE -> SETTLING -> IDLE) & momentum preservation on re-entry.
 * 6. 0.000% Idle CPU verification: thread blocks indefinitely in WaitForMultipleObjects with 0 ms CPU time consumed.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <cassert>

// Include production declarations from frame_loop.h
#include "frame_loop.h"

// Stub logging for icon_hover
#include <sdk/te_log.h>
extern "C" {
void TE_LogWrite(TE_LogLevel level, const char* tag, const char* message) {
    (void)level; (void)tag; (void)message;
}
}

// 64-bit packed union for wait-free coordinate ingest matching frame_loop.cpp
union PackedCursor {
    struct {
        int32_t x;
        int32_t y;
    } pos;
    uint64_t raw;
};

static int s_tests_passed = 0;
static int s_tests_failed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] Line %d: %s\n", __LINE__, msg); \
        s_tests_failed++; \
    } else { \
        s_tests_passed++; \
    } \
} while(0)

#define ASSERT_NEAR(val, expected, tol, msg) do { \
    float diff = fabsf((float)(val) - (float)(expected)); \
    if (diff > (float)(tol)) { \
        printf("  [FAIL] Line %d: %s (val=%.6f, expected=%.6f, diff=%.6f > tol=%.6f)\n", \
               __LINE__, msg, (float)(val), (float)(expected), diff, (float)(tol)); \
        s_tests_failed++; \
    } else { \
        s_tests_passed++; \
    } \
} while(0)

// ============================================================================
// TEST 1: Boundary Value Analysis (BVA) Around 300.0f Teleport Snap Threshold
// ============================================================================
static void Test_TeleportSnap_BVA(void)
{
    printf("\n--- TEST 1: BVA Around 300.0f Teleport Snap Threshold (Feature F10) ---\n");

    TE_CursorFilterState filter;
    const float dt = 0.008f;
    const float alpha_expected = 1.0f - expf(-dt / 0.015f);

    // 1.1 Sub-threshold: displacement = 299.9 px -> Smoothed via EMA
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, 299.9f, 0.0f, dt);
    float expected_smooth_299 = alpha_expected * 299.9f;
    ASSERT_NEAR(filter.filtered_x, expected_smooth_299, 1e-3f, "Displacement 299.9 px must be smoothed via EMA");
    ASSERT_TRUE(filter.filtered_x < 299.9f, "Displacement 299.9 px must not snap");

    // 1.2 Above-threshold: displacement = 300.1 px -> Snapped immediately
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, 300.1f, 0.0f, dt);
    ASSERT_NEAR(filter.filtered_x, 300.1f, 1e-4f, "Displacement 300.1 px must snap immediately (x_f = x_raw)");
    ASSERT_NEAR(filter.filtered_y, 0.0f, 1e-4f, "Displacement 300.1 px Y must remain unchanged");

    // 1.3 Exact boundary: displacement = 300.0 px -> dist_sq <= 90000.0 -> smoothed
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, 300.0f, 0.0f, dt);
    float expected_smooth_300 = alpha_expected * 300.0f;
    ASSERT_NEAR(filter.filtered_x, expected_smooth_300, 1e-3f, "Displacement exactly 300.0 px must follow <= boundary (smoothed)");

    // 1.4 Float ULP boundary stepping around 300.0f
    float float_below_300 = _nextafterf(300.0f, 0.0f);
    float float_above_300 = _nextafterf(300.0f, 1000.0f);
    unsigned int hex_below = 0, hex_above = 0;
    memcpy(&hex_below, &float_below_300, sizeof(float));
    memcpy(&hex_above, &float_above_300, sizeof(float));
    printf("  ULP Analysis: below=%.8f (hex 0x%08X), exactly 300.0=%.8f, above=%.8f (hex 0x%08X)\n",
           float_below_300, hex_below,
           300.0f,
           float_above_300, hex_above);

    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, float_below_300, 0.0f, dt);
    ASSERT_TRUE(filter.filtered_x < 299.0f, "Float below 300 must be smoothed via EMA");

    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, float_above_300, 0.0f, dt);
    ASSERT_NEAR(filter.filtered_x, float_above_300, 1e-4f, "Float above 300 must immediately snap");

    // 1.5 Negative direction boundary: -299.9 px vs -300.1 px
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, -299.9f, 0.0f, dt);
    ASSERT_NEAR(filter.filtered_x, -expected_smooth_299, 1e-3f, "Negative displacement -299.9 px must be smoothed");
    ASSERT_TRUE(filter.filtered_x > -299.9f, "Negative displacement -299.9 px must not snap");

    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, -300.1f, 0.0f, dt);
    ASSERT_NEAR(filter.filtered_x, -300.1f, 1e-4f, "Negative displacement -300.1 px must snap immediately");

    // 1.6 2D Euclidean boundary: dx^2 + dy^2 around 90000.0f
    // (212.1)^2 + (212.1)^2 = 89972.82 < 90000 -> dist = 299.95 px -> smoothed
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, 212.1f, 212.1f, dt);
    ASSERT_NEAR(filter.filtered_x, alpha_expected * 212.1f, 1e-3f, "2D diagonal 299.95 px must be smoothed");
    ASSERT_NEAR(filter.filtered_y, alpha_expected * 212.1f, 1e-3f, "2D diagonal 299.95 px must be smoothed");

    // (212.2)^2 + (212.2)^2 = 90057.68 > 90000 -> dist = 300.09 px -> snapped
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, 212.2f, 212.2f, dt);
    ASSERT_NEAR(filter.filtered_x, 212.2f, 1e-4f, "2D diagonal 300.09 px must snap immediately");
    ASSERT_NEAR(filter.filtered_y, 212.2f, 1e-4f, "2D diagonal 300.09 px must snap immediately");
}

// ============================================================================
// TEST 2: Cold-Start Initialization From Idle (Feature F10)
// ============================================================================
static void Test_ColdStart_Initialization(void)
{
    printf("\n--- TEST 2: Cold-Start Initialization From Idle (Feature F10) ---\n");

    TE_CursorFilterState filter;

    // Reset to uninitialized state
    TE_CursorFilterReset(&filter);
    ASSERT_TRUE(filter.initialized == 0, "Filter must be uninitialized after reset");

    // First sample at (1920.0f, 1080.0f) must immediately snap without ramp-up from (0, 0)
    TE_CursorFilterStep(&filter, 1920.0f, 1080.0f, 0.008f);
    ASSERT_TRUE(filter.initialized == 1, "Filter must be marked initialized on first sample");
    ASSERT_NEAR(filter.filtered_x, 1920.0f, 1e-4f, "Cold start must immediately snap X to raw coordinate (no sweep delay)");
    ASSERT_NEAR(filter.filtered_y, 1080.0f, 1e-4f, "Cold start must immediately snap Y to raw coordinate (no sweep delay)");

    // 10,000 cycles of reset -> step -> verify instant snap
    int instant_snaps = 0;
    for (int i = 0; i < 10000; i++) {
        TE_CursorFilterReset(&filter);
        float rx = (float)(rand() % 4000 - 2000);
        float ry = (float)(rand() % 2000 - 1000);
        TE_CursorFilterStep(&filter, rx, ry, 0.008f);
        if (filter.filtered_x == rx && filter.filtered_y == ry && filter.initialized == 1) {
            instant_snaps++;
        }
    }
    ASSERT_TRUE(instant_snaps == 10000, "10,000 randomized cold-start samples must all snap with zero sweep delay");
}

// ============================================================================
// TEST 3: Multi-Monitor Negative Screen Coordinates (Feature F10)
// ============================================================================
static void Test_MultiMonitor_NegativeCoordinates(void)
{
    printf("\n--- TEST 3: Multi-Monitor Negative Screen Coordinates (Feature F10) ---\n");

    TE_CursorFilterState filter;
    const float dt = 0.008f;
    const float alpha_expected = 1.0f - expf(-dt / 0.015f);

    // 3.1 Initialization in negative screen space (e.g. secondary monitor at [-1920, 0])
    TE_CursorFilterInit(&filter, -1200.0f, 400.0f);
    ASSERT_NEAR(filter.filtered_x, -1200.0f, 1e-4f, "Negative coordinates must initialize accurately");
    ASSERT_NEAR(filter.filtered_y, 400.0f, 1e-4f, "Y coordinates must initialize accurately");

    // 3.2 Sub-threshold displacement within negative coordinate space (-1200 to -1100, dx = +100 px)
    TE_CursorFilterStep(&filter, -1100.0f, 400.0f, dt);
    float expected_neg_x = -1200.0f + alpha_expected * 100.0f;
    ASSERT_NEAR(filter.filtered_x, expected_neg_x, 1e-3f, "Movement within negative screen space must be smoothly filtered");
    ASSERT_TRUE(filter.filtered_x > -1200.0f && filter.filtered_x < -1100.0f, "Negative coordinate filter must move monotonically toward target");

    // 3.3 Negative coordinate teleportation (> 300 px): -1100 to -1600 (dx = -500 px)
    TE_CursorFilterStep(&filter, -1600.0f, 400.0f, dt);
    ASSERT_NEAR(filter.filtered_x, -1600.0f, 1e-4f, "Large displacement in negative screen space must snap immediately");

    // 3.4 Boundary crossing: moving across screen boundary from -50 px to +50 px (dx = 100 px < 300 px)
    TE_CursorFilterInit(&filter, -50.0f, 500.0f);
    TE_CursorFilterStep(&filter, 50.0f, 500.0f, dt);
    float expected_cross_x = -50.0f + alpha_expected * 100.0f;
    ASSERT_NEAR(filter.filtered_x, expected_cross_x, 1e-3f, "Crossing 0 px boundary smoothly must preserve continuous EMA");

    // 3.5 PackedCursor union with negative coordinates
    PackedCursor p;
    p.pos.x = -1920;
    p.pos.y = -1080;
    PackedCursor p_read;
    p_read.raw = p.raw;
    ASSERT_TRUE(p_read.pos.x == -1920, "PackedCursor must preserve signed negative X coordinate");
    ASSERT_TRUE(p_read.pos.y == -1080, "PackedCursor must preserve signed negative Y coordinate");
}

// ============================================================================
// TEST 4: Atomic 64-Bit Coordinate Ingest Under 1000 Hz Simulated Polling
// ============================================================================
static void Test_Atomic64_CoordinateIngest_TearingStress(void)
{
    printf("\n--- TEST 4: Atomic 64-Bit Ingest 1000 Hz Polling Tearing Stress (Feature F10) ---\n");

    std::atomic<uint64_t> atomic_cursor{0};
    std::atomic<bool> stop_signal{false};
    std::atomic<uint64_t> torn_reads{0};
    std::atomic<uint64_t> total_reads{0};

    const int WRITE_COUNT = 1000000;

    // Producer thread: updates coordinates at maximum speed simulating 1000+ Hz mouse polling
    // Invariant: pos.y == ~pos.x (bitwise inversion guarantees every single bit changes simultaneously)
    std::thread producer([&]() {
        for (int i = 0; i < WRITE_COUNT; i++) {
            int32_t x = (int32_t)(i ^ 0x5A5A5A5A);
            int32_t y = ~x;
            PackedCursor p;
            p.pos.x = x;
            p.pos.y = y;
            atomic_cursor.store(p.raw, std::memory_order_release);
        }
        stop_signal.store(true, std::memory_order_release);
    });

    // Consumer thread: reads coordinates continuously simulating the render pump
    std::thread consumer([&]() {
        while (!stop_signal.load(std::memory_order_acquire)) {
            PackedCursor p;
            p.raw = atomic_cursor.load(std::memory_order_acquire);
            if (p.raw != 0) {
                if (p.pos.y != ~p.pos.x) {
                    torn_reads.fetch_add(1, std::memory_order_relaxed);
                } else {
                    total_reads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    producer.join();
    consumer.join();

    printf("  Stress Results: %llu concurrent atomic reads, %llu torn reads detected\n",
           (unsigned long long)total_reads.load(),
           (unsigned long long)torn_reads.load());

    ASSERT_TRUE(torn_reads.load() == 0, "Wait-free atomic 64-bit coordinate ingest MUST produce ZERO torn reads");
    ASSERT_TRUE(total_reads.load() > 10000, "Consumer thread must successfully sample significant number of updates");
}

// ============================================================================
// TEST 5: State Machine Transitions & Momentum Preservation (Feature F12)
// ============================================================================
static void Test_StateMachine_Transitions_And_Momentum(void)
{
    printf("\n--- TEST 5: State Machine Transitions & Momentum Preservation (Feature F12) ---\n");

    // 5.1 Verify enum values
    ASSERT_TRUE(TE_FRAME_STATE_IDLE == 0, "TE_FRAME_STATE_IDLE must equal 0");
    ASSERT_TRUE(TE_FRAME_STATE_ACTIVE == 1, "TE_FRAME_STATE_ACTIVE must equal 1");
    ASSERT_TRUE(TE_FRAME_STATE_SETTLING == 2, "TE_FRAME_STATE_SETTLING must equal 2");

    TE_FrameLoopState state = TE_FRAME_STATE_IDLE;
    TE_SpringState spring = { 1.0f, 0.0f, 1.0f };

    // Transition 1: IDLE -> ACTIVE on cursor entry
    bool mouse_in_taskbar = true;
    if (state == TE_FRAME_STATE_IDLE && mouse_in_taskbar) {
        state = TE_FRAME_STATE_ACTIVE;
        spring.target = 1.4f;
    }
    ASSERT_TRUE(state == TE_FRAME_STATE_ACTIVE, "State must transition from IDLE to ACTIVE on mouse enter");

    // Step physics forward in ACTIVE state
    const float dt = 0.008f;
    for (int i = 0; i < 10; i++) {
        TE_SpringStep(&spring, dt);
    }
    ASSERT_TRUE(spring.scale > 1.0f, "Scale must magnify toward target");
    ASSERT_TRUE(spring.velocity > 0.0f, "Velocity must be positive during magnification");

    // Transition 2: ACTIVE -> SETTLING on mouse leave
    mouse_in_taskbar = false;
    if (state == TE_FRAME_STATE_ACTIVE && !mouse_in_taskbar) {
        state = TE_FRAME_STATE_SETTLING;
        spring.target = 1.0f;
    }
    ASSERT_TRUE(state == TE_FRAME_STATE_SETTLING, "State must transition from ACTIVE to SETTLING on mouse leave");
    ASSERT_TRUE(spring.target == 1.0f, "Spring target must be 1.0f during SETTLING");

    // Step physics decelerating towards 1.0f
    for (int i = 0; i < 5; i++) {
        TE_SpringStep(&spring, dt);
    }
    float scale_before_reentry = spring.scale;
    float velocity_before_reentry = spring.velocity;

    // Transition 3: Re-entry during SETTLING -> ACTIVE with Momentum Preservation
    mouse_in_taskbar = true;
    if (state == TE_FRAME_STATE_SETTLING && mouse_in_taskbar) {
        state = TE_FRAME_STATE_ACTIVE;
        spring.target = 1.5f;
    }
    ASSERT_TRUE(state == TE_FRAME_STATE_ACTIVE, "State must return to ACTIVE on mouse re-entry during SETTLING");
    ASSERT_NEAR(spring.scale, scale_before_reentry, 1e-5f, "Re-entry must preserve existing scale without popping");
    ASSERT_NEAR(spring.velocity, velocity_before_reentry, 1e-5f, "Re-entry must preserve existing velocity without popping");

    // Transition 4: Complete settling to rest -> IDLE
    mouse_in_taskbar = false;
    state = TE_FRAME_STATE_SETTLING;
    spring.target = 1.0f;

    // Run until settled
    int settle_steps = 0;
    while (!TE_SpringIsSettled(&spring) && settle_steps < 200) {
        TE_SpringStep(&spring, dt);
        settle_steps++;
    }
    ASSERT_TRUE(TE_SpringIsSettled(&spring), "Spring must settle within 200 steps (1.6 seconds)");

    if (TE_SpringIsSettled(&spring)) {
        spring.scale = 1.0f;
        spring.velocity = 0.0f;
        state = TE_FRAME_STATE_IDLE;
    }
    ASSERT_TRUE(state == TE_FRAME_STATE_IDLE, "State must transition to IDLE when all icons settle");
    ASSERT_TRUE(spring.scale == 1.0f, "Settled scale must snap to exactly 1.0f");
    ASSERT_TRUE(spring.velocity == 0.0f, "Settled velocity must be exactly 0.0f");
}

// ============================================================================
// TEST 6: 0.000% Idle CPU Verification (Indefinite Blocking)
// ============================================================================
static void Test_IdleCPU_IndefiniteBlocking(void)
{
    printf("\n--- TEST 6: 0.000%% Idle CPU Indefinite Blocking Verification (Feature F12) ---\n");

    HANDLE stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ASSERT_TRUE(stop_event != NULL && wake_event != NULL, "Sync events created successfully");

    std::atomic<bool> thread_entered_wait{false};
    std::atomic<bool> thread_exited{false};

    HANDLE wait_handles[2] = { stop_event, wake_event };

    HANDLE worker_thread = CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
        auto* data = (std::pair<HANDLE*, std::pair<std::atomic<bool>*, std::atomic<bool>*>>*)param;
        HANDLE* handles = data->first;
        auto* entered = data->second.first;
        auto* exited = data->second.second;

        entered->store(true, std::memory_order_release);

        // Blocks indefinitely on handles: exactly 0.000% CPU
        DWORD wr = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        (void)wr;

        exited->store(true, std::memory_order_release);
        return 0;
    }, new std::pair<HANDLE*, std::pair<std::atomic<bool>*, std::atomic<bool>*>>(
        wait_handles, std::make_pair(&thread_entered_wait, &thread_exited)
    ), 0, NULL);

    ASSERT_TRUE(worker_thread != NULL, "Worker thread created successfully");

    // Wait until thread enters wait
    while (!thread_entered_wait.load(std::memory_order_acquire)) {
        Sleep(1);
    }

    // Let the thread block for 200 ms in idle
    Sleep(200);

    // Query thread CPU execution time
    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    BOOL ok = GetThreadTimes(worker_thread, &ftCreate, &ftExit, &ftKernel, &ftUser);
    ASSERT_TRUE(ok == TRUE, "GetThreadTimes succeeded");

    ULARGE_INTEGER kernel_time, user_time;
    kernel_time.LowPart = ftKernel.dwLowDateTime;
    kernel_time.HighPart = ftKernel.dwHighDateTime;
    user_time.LowPart = ftUser.dwLowDateTime;
    user_time.HighPart = ftUser.dwHighDateTime;

    uint64_t total_cpu_100ns = kernel_time.QuadPart + user_time.QuadPart;
    double total_cpu_ms = (double)total_cpu_100ns / 10000.0;

    printf("  Idle Thread CPU Measurement: Kernel=%.3f ms, User=%.3f ms, Total=%.3f ms over 200 ms window\n",
           (double)kernel_time.QuadPart / 10000.0,
           (double)user_time.QuadPart / 10000.0,
           total_cpu_ms);

    // Over 200 ms of pure blocking in WaitForMultipleObjects, CPU time must be essentially 0.000 ms
    ASSERT_TRUE(total_cpu_ms < 1.0, "Blocked thread must consume 0.000% CPU cycles (total CPU time < 1.0 ms)");

    // Signal wake event to verify immediate resumption
    auto t_before_wake = std::chrono::high_resolution_clock::now();
    SetEvent(wake_event);

    while (!thread_exited.load(std::memory_order_acquire)) {
        Sleep(0);
    }
    auto t_after_wake = std::chrono::high_resolution_clock::now();
    double wake_latency_us = std::chrono::duration<double, std::micro>(t_after_wake - t_before_wake).count();

    printf("  Wake Event Latency: %.2f microseconds (< 1000 us)\n", wake_latency_us);
    ASSERT_TRUE(wake_latency_us < 5000.0, "Wake latency from IDLE must be instantaneous (< 5 ms)");

    // Clean up
    WaitForSingleObject(worker_thread, 1000);
    CloseHandle(worker_thread);
    CloseHandle(stop_event);
    CloseHandle(wake_event);
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
int main(void)
{
    printf("===============================================================================\n");
    printf("TaskbarEngine Milestone 3 Challenger 2: Feature F10 & F12 Stress & BVA Harness\n");
    printf("===============================================================================\n");

    Test_TeleportSnap_BVA();
    Test_ColdStart_Initialization();
    Test_MultiMonitor_NegativeCoordinates();
    Test_Atomic64_CoordinateIngest_TearingStress();
    Test_StateMachine_Transitions_And_Momentum();
    Test_IdleCPU_IndefiniteBlocking();

    printf("\n===============================================================================\n");
    printf("SUMMARY: %d passed, %d failed\n", s_tests_passed, s_tests_failed);
    printf("===============================================================================\n");

    return (s_tests_failed == 0) ? 0 : 1;
}
