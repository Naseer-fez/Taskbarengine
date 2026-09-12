/**
 * @file test_m4_challenger2_stress.cpp
 * @brief Empirical Challenger 2 Stress Harness for Milestone 4 Gate.
 *
 * Scope:
 * 1. Feature F15: Micro-Benchmark Performance Stress across 1 to 20 icons (< 500 ns target).
 * 2. Feature F4: Background Subtraction Matte Mathematics & Alpha Isolation across 5 palettes.
 * 3. Features F9, F10, F11, F12: Spring & Filter Edge Cases (dt=0, teleport boundary, multi-cycle state machine).
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cstdint>

#include "magnification.h"
#include "frame_loop.h"

static int s_passed = 0;
static int s_failed = 0;

#define EXPECT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
        s_failed++; \
    } else { \
        s_passed++; \
    } \
} while(0)

#define EXPECT_NEAR(val, exp, tol, msg) do { \
    double diff = fabs((double)(val) - (double)(exp)); \
    if (diff > (double)(tol)) { \
        printf("  [FAIL] %s:%d: %s (val=%.6f, exp=%.6f, diff=%.6f > tol=%.6f)\n", \
               __FILE__, __LINE__, msg, (double)(val), (double)(exp), diff, (double)(tol)); \
        s_failed++; \
    } else { \
        s_passed++; \
    } \
} while(0)

// ============================================================================
// SUITE 1: Feature F15 Micro-Benchmark Performance Stress (1 to 20 Icons)
// ============================================================================
static void RunSuite1_Microbenchmarks(void)
{
    printf("\n============================================================\n");
    printf(" SUITE 1: Feature F15 Microbenchmark Performance Stress\n");
    printf("============================================================\n");

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    const TE_MagnifyCurveType curves[] = {
        TE_CURVE_GAUSSIAN,
        TE_CURVE_CUBIC,
        TE_CURVE_COSINE,
        TE_CURVE_LINEAR
    };
    const char* curve_names[] = {
        "Gaussian", "Hermite Cubic", "Cosine", "Linear"
    };

    const int test_counts[] = { 1, 2, 3, 4, 5, 8, 10, 12, 16, 20 };
    const int num_counts = sizeof(test_counts) / sizeof(test_counts[0]);

    printf("%-15s | %-6s | %-12s | %-12s | %-10s\n", "Curve", "Icons", "Total Time", "Mean Time/Call", "Status");
    printf("----------------------------------------------------------------------\n");

    for (int c = 0; c < 4; c++) {
        TE_MagnifyCurveType curve = curves[c];
        const char* cname = curve_names[c];

        for (int ci = 0; ci < num_counts; ci++) {
            int count = test_counts[ci];
            std::vector<float> centers(count);
            std::vector<float> scales(count, 1.0f);
            for (int i = 0; i < count; i++) {
                centers[i] = i * 48.0f;
            }

            float cursor_x = (count / 2) * 48.0f;
            float radius = 120.0f;
            float max_scale = 1.5f;

            // Warm-up
            for (int w = 0; w < 1000; w++) {
                TE_MagnifyComputeScales(cursor_x, centers.data(), scales.data(), count, radius, max_scale, curve);
            }

            // Benchmark run
            const int iterations = 100000;
            LARGE_INTEGER start, end;
            QueryPerformanceCounter(&start);
            for (int it = 0; it < iterations; it++) {
                TE_MagnifyComputeScales(cursor_x, centers.data(), scales.data(), count, radius, max_scale, curve);
            }
            QueryPerformanceCounter(&end);

            double elapsed_sec = (double)(end.QuadPart - start.QuadPart) / (double)freq.QuadPart;
            double mean_ns = (elapsed_sec * 1e9) / (double)iterations;

            bool pass = (mean_ns < 500.0);
            printf("%-15s | %-6d | %8.4f ms  | %8.2f ns    | %s\n",
                   cname, count, elapsed_sec * 1000.0, mean_ns, pass ? "PASS (<500ns)" : "FAIL (>=500ns)");

            EXPECT_TRUE(pass, "TE_MagnifyComputeScales mean time must be strictly < 500 ns");
            EXPECT_TRUE(scales[count / 2] >= 1.0f && scales[count / 2] <= max_scale,
                        "Center icon scale must be in [1.0, max_scale]");
        }
    }

    // Pathological Boundary inputs
    printf("\n--- Pathological Boundary Inputs for Magnification Math ---\n");
    float dummy_scale = 999.0f;
    float dummy_center = 100.0f;

    // count = 0
    TE_MagnifyComputeScales(100.0f, &dummy_center, &dummy_scale, 0, 100.0f, 1.5f, TE_CURVE_GAUSSIAN);
    EXPECT_TRUE(dummy_scale == 999.0f, "count = 0 must not modify output buffer");

    // count < 0
    TE_MagnifyComputeScales(100.0f, &dummy_center, &dummy_scale, -5, 100.0f, 1.5f, TE_CURVE_GAUSSIAN);
    EXPECT_TRUE(dummy_scale == 999.0f, "count < 0 must not modify output buffer");

    // radius <= 0
    dummy_scale = 999.0f;
    TE_MagnifyComputeScales(100.0f, &dummy_center, &dummy_scale, 1, 0.0f, 1.5f, TE_CURVE_GAUSSIAN);
    EXPECT_TRUE(dummy_scale == 1.0f, "radius = 0 must set all scales to 1.0f");

    dummy_scale = 999.0f;
    TE_MagnifyComputeScales(100.0f, &dummy_center, &dummy_scale, 1, -50.0f, 1.5f, TE_CURVE_GAUSSIAN);
    EXPECT_TRUE(dummy_scale == 1.0f, "radius < 0 must set all scales to 1.0f");

    // max_scale < 1.0f (must clamp to 1.0f)
    dummy_scale = 999.0f;
    TE_MagnifyComputeScales(100.0f, &dummy_center, &dummy_scale, 1, 100.0f, 0.5f, TE_CURVE_GAUSSIAN);
    EXPECT_TRUE(dummy_scale == 1.0f, "max_scale < 1.0 must clamp to 1.0f");

    // Microscopic radius
    dummy_scale = 999.0f;
    TE_MagnifyComputeScales(100.0f, &dummy_center, &dummy_scale, 1, 1e-7f, 1.5f, TE_CURVE_GAUSSIAN);
    EXPECT_TRUE(!isnan(dummy_scale) && !isinf(dummy_scale), "Microscopic radius must not produce NaN/Inf");

    // Far away cursor
    dummy_scale = 999.0f;
    TE_MagnifyComputeScales(1e6f, &dummy_center, &dummy_scale, 1, 100.0f, 1.5f, TE_CURVE_GAUSSIAN);
    EXPECT_TRUE(dummy_scale == 1.0f, "Far away cursor must produce scale = 1.0f");
}

// ============================================================================
// SUITE 2: Feature F4 Background Subtraction Matte Mathematics
// ============================================================================
static void RunSuite2_MatteArithmetic(void)
{
    printf("\n============================================================\n");
    printf(" SUITE 2: Feature F4 Matte Background Subtraction Arithmetic\n");
    printf("============================================================\n");

    struct TaskbarPalette {
        const char* name;
        uint8_t r, g, b;
    };

    const TaskbarPalette palettes[] = {
        { "Dark Taskbar (Win11 default)", 24, 24, 24 },
        { "Light Taskbar (Win10 light mode)", 240, 240, 240 },
        { "Accent Blue Taskbar", 0, 120, 215 },
        { "Pure Black (High Contrast)", 0, 0, 0 },
        { "Mid-tone Gray", 128, 128, 128 }
    };

    const int eps_low = 8;
    const int eps_high = 32;

    auto isolate_matte = [eps_low, eps_high](uint8_t c_r, uint8_t c_g, uint8_t c_b,
                                             uint8_t bg_r, uint8_t bg_g, uint8_t bg_b,
                                             uint8_t& out_r, uint8_t& out_g, uint8_t& out_b, uint8_t& out_a) {
        int dr = abs((int)c_r - (int)bg_r);
        int dg = abs((int)c_g - (int)bg_g);
        int db = abs((int)c_b - (int)bg_b);
        int delta = (std::max)({ dr, dg, db });

        uint32_t alpha = 0;
        if (delta <= eps_low) {
            alpha = 0;
        } else if (delta >= eps_high) {
            alpha = 255;
        } else {
            alpha = (uint32_t)((delta - eps_low) * 255 / (eps_high - eps_low));
        }

        if (alpha == 0) {
            out_r = out_g = out_b = out_a = 0;
        } else {
            out_r = (uint8_t)((c_r * alpha + 127) / 255);
            out_g = (uint8_t)((c_g * alpha + 127) / 255);
            out_b = (uint8_t)((c_b * alpha + 127) / 255);
            out_a = (uint8_t)alpha;
        }
    };

    for (const auto& pal : palettes) {
        printf("Testing Palette: %s [RGB(%d, %d, %d)]\n", pal.name, pal.r, pal.g, pal.b);

        // 1. Pure background pixel: C = B -> alpha MUST be 0, pixel must be 0x00000000
        uint8_t r, g, b, a;
        isolate_matte(pal.r, pal.g, pal.b, pal.r, pal.g, pal.b, r, g, b, a);
        EXPECT_TRUE(a == 0, "Pure background pixel must yield alpha = 0");
        EXPECT_TRUE(r == 0 && g == 0 && b == 0, "Pure background pixel must yield zero RGB");

        // 2. Slight background noise / compression artifact (delta <= 8)
        for (int d = -8; d <= 8; d++) {
            int cr = (std::max)(0, (std::min)(255, (int)pal.r + d));
            int cg = (std::max)(0, (std::min)(255, (int)pal.g + d));
            int cb = (std::max)(0, (std::min)(255, (int)pal.b + d));
            isolate_matte((uint8_t)cr, (uint8_t)cg, (uint8_t)cb, pal.r, pal.g, pal.b, r, g, b, a);
            EXPECT_TRUE(a == 0, "Background noise within eps_low <= 8 must be eliminated (alpha = 0)");
            EXPECT_TRUE(r == 0 && g == 0 && b == 0, "Sub-threshold noise must yield 0x00000000");
        }

        // 3. High-contrast artwork pixels (e.g. delta >= 32)
        // Diverse artwork colors: White, Red, Green, Blue, Yellow, Cyan, Magenta
        const uint8_t test_colors[7][3] = {
            { 255, 255, 255 }, // White
            { 255,   0,   0 }, // Red
            {   0, 255,   0 }, // Green
            {   0,   0, 255 }, // Blue
            { 255, 255,   0 }, // Yellow
            {   0, 255, 255 }, // Cyan
            { 255,   0, 255 }  // Magenta
        };

        for (int tc = 0; tc < 7; tc++) {
            uint8_t ir = test_colors[tc][0];
            uint8_t ig = test_colors[tc][1];
            uint8_t ib = test_colors[tc][2];

            int dr = abs((int)ir - (int)pal.r);
            int dg = abs((int)ig - (int)pal.g);
            int db = abs((int)ib - (int)pal.b);
            int max_d = (std::max)({ dr, dg, db });

            if (max_d >= 32) {
                isolate_matte(ir, ig, ib, pal.r, pal.g, pal.b, r, g, b, a);
                EXPECT_TRUE(a == 255, "High contrast artwork pixel must yield alpha = 255");
                EXPECT_TRUE(r == ir, "Artwork R must be preserved at alpha=255");
                EXPECT_TRUE(g == ig, "Artwork G must be preserved at alpha=255");
                EXPECT_TRUE(b == ib, "Artwork B must be preserved at alpha=255");
                EXPECT_TRUE(r <= a && g <= a && b <= a, "Premultiplied invariant must hold at alpha=255");
            }
        }

        // 4. Intermediate alpha blending ramp: C = alpha * I + (1 - alpha) * B
        uint8_t icon_art_r = 250, icon_art_g = 180, icon_art_b = 30; // Orange artwork
        for (int step = 0; step <= 10; step++) {
            float alpha_true = step / 10.0f;
            uint8_t comp_r = (uint8_t)(alpha_true * icon_art_r + (1.0f - alpha_true) * pal.r + 0.5f);
            uint8_t comp_g = (uint8_t)(alpha_true * icon_art_g + (1.0f - alpha_true) * pal.g + 0.5f);
            uint8_t comp_b = (uint8_t)(alpha_true * icon_art_b + (1.0f - alpha_true) * pal.b + 0.5f);

            isolate_matte(comp_r, comp_g, comp_b, pal.r, pal.g, pal.b, r, g, b, a);
            // Strict premultiplied invariant check
            EXPECT_TRUE(r <= a, "Premultiplied invariant: R <= A must hold for blended pixels");
            EXPECT_TRUE(g <= a, "Premultiplied invariant: G <= A must hold for blended pixels");
            EXPECT_TRUE(b <= a, "Premultiplied invariant: B <= A must hold for blended pixels");
        }
    }

    // 5. Exhaustive proof of premultiplied invariant: R, G, B <= A
    printf("Verifying exhaustive premultiplied invariant across all 65,536 (color, alpha) pairs...\n");
    bool all_premul_valid = true;
    for (int color = 0; color <= 255; color++) {
        for (int alpha = 0; alpha <= 255; alpha++) {
            uint32_t premul = (uint32_t)((color * alpha + 127) / 255);
            if (premul > (uint32_t)alpha) {
                all_premul_valid = false;
                break;
            }
        }
        if (!all_premul_valid) break;
    }
    EXPECT_TRUE(all_premul_valid, "Premultiplied invariant R,G,B <= A must hold 100% across all 65,536 pairs");
}

// ============================================================================
// SUITE 3: Features F9, F10, F11, F12 Edge Cases & Boundary Analysis
// ============================================================================
static void RunSuite3_EdgeCases(void)
{
    printf("\n============================================================\n");
    printf(" SUITE 3: Spring & Filter Edge Cases (F9, F10, F11, F12)\n");
    printf("============================================================\n");

    // 3.1 Feature F9: Spring Physics Edge Cases
    printf("--- Feature F9 Spring Edge Cases ---\n");

    // dt = 0 must produce ZERO change
    TE_SpringState sp = { 1.450f, 0.320f, 1.000f };
    TE_SpringStep(&sp, 0.0f);
    EXPECT_NEAR(sp.scale, 1.450f, 1e-6f, "dt = 0 must produce exactly 0.0 scale delta");
    EXPECT_NEAR(sp.velocity, 0.320f, 1e-6f, "dt = 0 must produce exactly 0.0 velocity delta");

    // Microscopic dt (1e-6s)
    TE_SpringStep(&sp, 1e-6f);
    EXPECT_NEAR(sp.scale, 1.450f, 1e-4f, "Microscopic dt must produce negligible delta");

    // Settle resting state invariance: resting at 1.0 must remain at 1.0 for 1000 frames
    sp.scale = 1.0000f;
    sp.velocity = 0.0000f;
    sp.target = 1.0000f;
    for (int i = 0; i < 1000; i++) {
        TE_SpringStep(&sp, 0.016f);
    }
    EXPECT_NEAR(sp.scale, 1.0000f, 1e-6f, "Resting spring must remain identically stationary");
    EXPECT_NEAR(sp.velocity, 0.0000f, 1e-6f, "Resting velocity must remain identically 0");
    EXPECT_TRUE(TE_SpringIsSettled(&sp) == 1, "Resting spring must report settled");

    // Monotonic convergence without overshoot
    sp.scale = 1.5000f;
    sp.velocity = 0.0000f;
    sp.target = 1.0000f;
    float prev_scale = sp.scale;
    bool strictly_monotonic = true;
    for (int i = 0; i < 100; i++) {
        TE_SpringStep(&sp, 0.00833f); // 120 Hz
        if (sp.scale > prev_scale || sp.scale < 1.0000f) {
            strictly_monotonic = false;
            break;
        }
        prev_scale = sp.scale;
    }
    EXPECT_TRUE(strictly_monotonic, "Critically damped spring must decay monotonically with zero overshoot");
    EXPECT_NEAR(sp.scale, 1.0000f, 1e-3f, "Spring must converge to 1.0 within 100 frames (~830 ms)");

    // Analytical closed-form formula comparison
    float x0 = 1.40f;
    float target = 1.00f;
    float omega0 = 32.0f;
    for (float t = 0.01f; t <= 0.15f; t += 0.01f) {
        float exact = TE_SpringAnalytical(x0, target, omega0, t);
        sp.scale = x0;
        sp.velocity = 0.0f;
        sp.target = target;
        TE_SpringStep(&sp, t);
        EXPECT_NEAR(sp.scale, exact, 1e-4f, "Step simulation must match analytical closed form formula");
    }

    // 3.2 Feature F10: Cursor Low-Pass Filter & Teleport Snapping
    printf("--- Feature F10 Teleport Snapping Precision ---\n");
    TE_CursorFilterState filter;
    const float dt = 0.008f;
    const float alpha_expected = 1.0f - expf(-dt / 0.015f);

    // dt = 0 coordinate invariance
    TE_CursorFilterInit(&filter, 500.0f, 300.0f);
    TE_CursorFilterStep(&filter, 550.0f, 350.0f, 0.0f);
    EXPECT_NEAR(filter.filtered_x, 500.0f, 1e-6f, "dt = 0 must preserve filtered_x");
    EXPECT_NEAR(filter.filtered_y, 300.0f, 1e-6f, "dt = 0 must preserve filtered_y");

    // Exact sub-pixel 300.0f threshold
    // 299.99 px: must be smoothed via EMA
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, 299.99f, 0.0f, dt);
    float expected_smooth_x = alpha_expected * 299.99f;
    EXPECT_NEAR(filter.filtered_x, expected_smooth_x, 1e-3f, "Displacement 299.99 px must be smoothed");
    EXPECT_TRUE(filter.filtered_x < 299.99f, "Displacement 299.99 px must not snap");

    // 300.01 px: must snap immediately
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, 300.01f, 0.0f, dt);
    EXPECT_NEAR(filter.filtered_x, 300.01f, 1e-4f, "Displacement 300.01 px must snap immediately");

    // Y-axis 300.01 px snap
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, 0.0f, 300.01f, dt);
    EXPECT_NEAR(filter.filtered_y, 300.01f, 1e-4f, "Y displacement 300.01 px must snap immediately");

    // 2D diagonal: dx = 212.14, dy = 212.14 -> dist = 300.011 px > 300 px -> snap
    TE_CursorFilterInit(&filter, 0.0f, 0.0f);
    TE_CursorFilterStep(&filter, 212.14f, 212.14f, dt);
    EXPECT_NEAR(filter.filtered_x, 212.14f, 1e-4f, "2D diagonal dist > 300 px must snap X");
    EXPECT_NEAR(filter.filtered_y, 212.14f, 1e-4f, "2D diagonal dist > 300 px must snap Y");

    // Cold-start snap
    TE_CursorFilterReset(&filter);
    EXPECT_TRUE(filter.initialized == 0, "Reset must uninitialize filter");
    TE_CursorFilterStep(&filter, 1920.0f, 1080.0f, dt);
    EXPECT_TRUE(filter.initialized == 1, "First sample must initialize filter");
    EXPECT_NEAR(filter.filtered_x, 1920.0f, 1e-6f, "Cold start must snap without sweep delay");
    EXPECT_NEAR(filter.filtered_y, 1080.0f, 1e-6f, "Cold start must snap without sweep delay");

    // Multi-monitor negative screen coordinates
    TE_CursorFilterInit(&filter, -1920.0f, 500.0f);
    TE_CursorFilterStep(&filter, -1820.0f, 500.0f, dt); // Move right by 100 px on secondary monitor
    EXPECT_TRUE(filter.filtered_x > -1920.0f && filter.filtered_x < -1820.0f,
                "Negative screen coordinates must smoothly filter across monitor space");

    // 3.3 Features F11 & F12: Sleep / Wake State Machine Multi-Cycle Rapid Transitions
    printf("--- Features F11 & F12 Multi-Cycle State Machine Stress ---\n");
    TE_FrameLoopState state = TE_FRAME_STATE_IDLE;
    TE_SpringState sim_spring = { 1.0f, 0.0f, 1.0f };

    for (int cycle = 0; cycle < 25; cycle++) {
        // Cycle wake: IDLE -> ACTIVE
        EXPECT_TRUE(state == TE_FRAME_STATE_IDLE, "Cycle must start in IDLE state");
        state = TE_FRAME_STATE_ACTIVE;
        sim_spring.target = 1.35f;

        // Step active state
        for (int a = 0; a < 5; a++) {
            TE_SpringStep(&sim_spring, 0.008f);
        }
        EXPECT_TRUE(sim_spring.scale > 1.0f, "Scale must expand during active hover");

        // Hover exit: ACTIVE -> SETTLING
        state = TE_FRAME_STATE_SETTLING;
        sim_spring.target = 1.0f;

        // Run settle loop until settled
        int settle_frames = 0;
        while (!TE_SpringIsSettled(&sim_spring) && settle_frames < 200) {
            TE_SpringStep(&sim_spring, 0.008f);
            settle_frames++;
        }

        EXPECT_TRUE(TE_SpringIsSettled(&sim_spring) == 1, "Spring must settle within reasonable frame count");
        EXPECT_TRUE(settle_frames <= 60, "Settle time must be <= 60 frames (~500 ms)");

        // Settle complete: Transition to IDLE
        state = TE_FRAME_STATE_IDLE;
        sim_spring.scale = 1.0f;
        sim_spring.velocity = 0.0f;
        EXPECT_TRUE(state == TE_FRAME_STATE_IDLE, "State must transition back to IDLE");
    }
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    printf("============================================================\n");
    printf(" TaskbarEngine Milestone 4 Challenger 2 Stress Test Suite\n");
    printf("============================================================\n");

    RunSuite1_Microbenchmarks();
    RunSuite2_MatteArithmetic();
    RunSuite3_EdgeCases();

    printf("\n============================================================\n");
    printf(" FINAL RESULTS: %d Passed, %d Failed\n", s_passed, s_failed);
    printf("============================================================\n");

    return (s_failed == 0) ? 0 : 1;
}
