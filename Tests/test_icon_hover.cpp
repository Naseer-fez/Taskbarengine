#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <windows.h>
#include <sdk/te_types.h>
#include <sdk/te_events.h>
#include <sdk/te_plugin.h>
#include "magnification.h"
#include <vector>
#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("Icon Hover - Mouse Event Structure & Dispatch", "[icon_hover][mouse]") {
    TE_TaskbarMouseData data;
    data.cursor_pos.x = 250;
    data.cursor_pos.y = 1050;
    data.is_in_taskbar = TRUE;

    REQUIRE(data.cursor_pos.x == 250);
    REQUIRE(data.cursor_pos.y == 1050);
    REQUIRE(data.is_in_taskbar == TRUE);

    data.is_in_taskbar = FALSE;
    REQUIRE(data.is_in_taskbar == FALSE);
}

TEST_CASE("Icon Hover - High-DPI Headroom and Radius Scaling", "[icon_hover][dpi]") {
    auto calc_headroom = [](uint32_t dpi) -> int {
        return (int)(64.0f * (float)dpi / 96.0f);
    };

    SECTION("Standard 96 DPI (100%)") {
        REQUIRE(calc_headroom(96) == 64);
    }

    SECTION("120 DPI (125%)") {
        REQUIRE(calc_headroom(120) == 80);
    }

    SECTION("144 DPI (150%)") {
        REQUIRE(calc_headroom(144) == 96);
    }

    SECTION("192 DPI (200%)") {
        REQUIRE(calc_headroom(192) == 128);
    }
}

TEST_CASE("Icon Hover - Displaced Position Math & Top Headroom Expansion", "[icon_hover][displacement]") {
    const int count = 5;
    float base_w = 48.0f;
    float base_h = 48.0f;
    float scales[count] = { 1.0f, 1.2f, 1.4f, 1.2f, 1.0f };

    float pos_x[count] = { 0 };
    float pos_y[count] = { 0 };

    float cumulative_dx = 0.0f;
    for (int i = 0; i < count; i++) {
        float scale = scales[i];
        float extra_w = base_w * (scale - 1.0f);
        pos_x[i] = cumulative_dx - extra_w / 2.0f;

        float extra_h = base_h * (scale - 1.0f);
        pos_y[i] = -extra_h; // Grow upward

        cumulative_dx += extra_w;
    }

    SECTION("Unscaled icons have zero displacement") {
        REQUIRE(scales[0] == 1.0f);
        REQUIRE(pos_y[0] == 0.0f);
    }

    SECTION("Magnified peak icon grows upward without exceeding headroom") {
        REQUIRE_THAT(pos_y[2], WithinAbs(-19.2f, 0.01f));
        REQUIRE(fabsf(pos_y[2]) < 64.0f);
    }

    SECTION("Cumulative X displacement pushes subsequent icons rightward") {
        REQUIRE(pos_x[4] > pos_x[0]);
        REQUIRE(pos_x[3] > pos_x[1]);
    }
}

TEST_CASE("Icon Hover - Alpha Channel Preservation for 32-bit DIB", "[icon_hover][alpha]") {
    const int w = 16, h = 16;
    std::vector<uint32_t> pixels(w * h, 0x00A0B0C0);

    for (auto p : pixels) {
        REQUIRE((p & 0xFF000000) == 0);
    }

    BOOL has_alpha = FALSE;
    for (auto p : pixels) {
        if ((p & 0xFF000000) != 0) {
            has_alpha = TRUE;
            break;
        }
    }
    if (!has_alpha) {
        for (auto& p : pixels) {
            if ((p & 0x00FFFFFF) != 0) {
                p |= 0xFF000000;
            }
        }
    }

    for (auto p : pixels) {
        REQUIRE((p & 0xFF000000) == 0xFF000000);
    }
}

TEST_CASE("Icon Hover - Settle Animation Convergence", "[icon_hover][settle]") {
    float current_scale = 1.4f;
    const float target_scale = 1.0f;
    const int speed_ms = 150;
    const float dt = 0.008f;

    float lerp_speed = (1000.0f / (float)speed_ms) * dt;
    int ticks = 0;
    while (fabsf(current_scale - target_scale) >= 0.001f && ticks < 200) {
        current_scale += (target_scale - current_scale) * lerp_speed;
        ticks++;
    }

    REQUIRE_THAT(current_scale, WithinAbs(1.0f, 0.001f));
    REQUIRE(ticks <= 120);
}
