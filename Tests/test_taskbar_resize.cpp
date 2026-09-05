#include <catch2/catch_test_macros.hpp>
#include "taskbar_resize.h"

TEST_CASE("Taskbar Resize - DPI Scaling", "[plugin][taskbar_resize][dpi]") {
    SECTION("Standard 96 DPI (100%)") {
        REQUIRE(TE_ScaleDPI(48, 96) == 48);
        REQUIRE(TE_ScaleDPI(36, 96) == 36);
        REQUIRE(TE_ScaleDPI(32, 96) == 32);
        REQUIRE(TE_ScaleDPI(30, 96) == 30);
        REQUIRE(TE_ScaleDPI(24, 96) == 24);
    }

    SECTION("120 DPI (125% Scale)") {
        REQUIRE(TE_ScaleDPI(48, 120) == 60);
        REQUIRE(TE_ScaleDPI(36, 120) == 45);
        REQUIRE(TE_ScaleDPI(32, 120) == 40);
        REQUIRE(TE_ScaleDPI(24, 120) == 30);
    }

    SECTION("144 DPI (150% Scale)") {
        REQUIRE(TE_ScaleDPI(48, 144) == 72);
        REQUIRE(TE_ScaleDPI(36, 144) == 54);
        REQUIRE(TE_ScaleDPI(32, 144) == 48);
        REQUIRE(TE_ScaleDPI(24, 144) == 36);
    }

    SECTION("192 DPI (200% Scale)") {
        REQUIRE(TE_ScaleDPI(48, 192) == 96);
        REQUIRE(TE_ScaleDPI(36, 192) == 72);
        REQUIRE(TE_ScaleDPI(32, 192) == 64);
        REQUIRE(TE_ScaleDPI(24, 192) == 48);
    }

    SECTION("Zero DPI Handling") {
        REQUIRE(TE_ScaleDPI(48, 0) == 0);
        REQUIRE(TE_ScaleDPI(32, 0) == 0);
    }
}

TEST_CASE("Taskbar Resize - Vertical Centering Offset Symmetry", "[plugin][taskbar_resize][centering]") {
    const uint32_t dpis[] = { 96, 120, 144, 192 };

    SECTION("Default 48px Taskbar Has Zero Offset") {
        for (uint32_t dpi : dpis) {
            int offset = TE_CalculateCenteringOffset(48, 48, 0, 0, dpi);
            REQUIRE(offset == 0);
        }
    }

    SECTION("32px Sweet Spot Centering Symmetry") {
        for (uint32_t dpi : dpis) {
            int bar_h = TE_ScaleDPI(32, dpi);
            int icon_h = TE_ScaleDPI(24, dpi);
            int def_top_pad = TE_ScaleDPI(12, dpi);

            int offset = TE_CalculateCenteringOffset(32, 48, 0, 0, dpi);
            int actual_top_pad = def_top_pad + offset;
            int actual_bot_pad = bar_h - (actual_top_pad + icon_h);

            // Verify symmetric centering: top padding == bottom padding
            REQUIRE(std::abs(actual_top_pad - actual_bot_pad) <= 1);
            REQUIRE(actual_top_pad > 0);
            REQUIRE(actual_bot_pad > 0);
        }
    }

    SECTION("36px Relaxed Compact Centering Symmetry") {
        for (uint32_t dpi : dpis) {
            int bar_h = TE_ScaleDPI(36, dpi);
            int icon_h = TE_ScaleDPI(24, dpi);
            int def_top_pad = TE_ScaleDPI(12, dpi);

            int offset = TE_CalculateCenteringOffset(36, 48, 0, 0, dpi);
            int actual_top_pad = def_top_pad + offset;
            int actual_bot_pad = bar_h - (actual_top_pad + icon_h);

            REQUIRE(std::abs(actual_top_pad - actual_bot_pad) <= 1);
            REQUIRE(actual_top_pad > 0);
            REQUIRE(actual_bot_pad > 0);
        }
    }

    SECTION("30px Compact Centering Symmetry") {
        for (uint32_t dpi : dpis) {
            int bar_h = TE_ScaleDPI(30, dpi);
            int icon_h = TE_ScaleDPI(24, dpi);
            int def_top_pad = TE_ScaleDPI(12, dpi);

            int offset = TE_CalculateCenteringOffset(30, 48, 0, 0, dpi);
            int actual_top_pad = def_top_pad + offset;
            int actual_bot_pad = bar_h - (actual_top_pad + icon_h);

            REQUIRE(std::abs(actual_top_pad - actual_bot_pad) <= 1);
            REQUIRE(actual_top_pad >= 0);
            REQUIRE(actual_bot_pad >= 0);
        }
    }

    SECTION("24px Ultra-Thin Centering (Edge to Edge)") {
        for (uint32_t dpi : dpis) {
            int bar_h = TE_ScaleDPI(24, dpi);
            int icon_h = TE_ScaleDPI(24, dpi);
            int def_top_pad = TE_ScaleDPI(12, dpi);

            int offset = TE_CalculateCenteringOffset(24, 48, 0, 0, dpi);
            int actual_top_pad = def_top_pad + offset;
            int actual_bot_pad = bar_h - (actual_top_pad + icon_h);

            REQUIRE(std::abs(actual_top_pad - actual_bot_pad) <= 1);
            REQUIRE(actual_top_pad >= 0);
            REQUIRE(actual_bot_pad >= 0);
        }
    }

    SECTION("Custom Padding Adjustments") {
        int base_offset = TE_CalculateCenteringOffset(32, 48, 0, 0, 96);
        int nudge_down = TE_CalculateCenteringOffset(32, 48, 4, 0, 96);
        int nudge_up = TE_CalculateCenteringOffset(32, 48, 0, 4, 96);

        REQUIRE(nudge_down == base_offset + 2);
        REQUIRE(nudge_up == base_offset - 2);
    }
}

TEST_CASE("Taskbar Resize - Desktop WorkArea Across DPIs", "[plugin][taskbar_resize][workarea]") {
    RECT monitor_1080p = { 0, 0, 1920, 1080 };
    RECT monitor_1440p = { 0, 0, 2560, 1440 };
    RECT monitor_4k    = { 0, 0, 3840, 2160 };

    SECTION("1080p at 96 DPI (100%) - 32px Height") {
        RECT wa;
        REQUIRE(TE_CalculateWorkArea(&monitor_1080p, 32, 96, &wa) == TRUE);
        REQUIRE(wa.left == 0);
        REQUIRE(wa.top == 0);
        REQUIRE(wa.right == 1920);
        REQUIRE(wa.bottom == 1080 - 32); // 1048
    }

    SECTION("1080p at 120 DPI (125%) - 32px Height") {
        RECT wa;
        REQUIRE(TE_CalculateWorkArea(&monitor_1080p, 32, 120, &wa) == TRUE);
        REQUIRE(wa.left == 0);
        REQUIRE(wa.top == 0);
        REQUIRE(wa.right == 1920);
        REQUIRE(wa.bottom == 1080 - 40); // 1040
    }

    SECTION("1440p at 144 DPI (150%) - 32px Height") {
        RECT wa;
        REQUIRE(TE_CalculateWorkArea(&monitor_1440p, 32, 144, &wa) == TRUE);
        REQUIRE(wa.left == 0);
        REQUIRE(wa.top == 0);
        REQUIRE(wa.right == 2560);
        REQUIRE(wa.bottom == 1440 - 48); // 1392
    }

    SECTION("4K at 192 DPI (200%) - 32px Height") {
        RECT wa;
        REQUIRE(TE_CalculateWorkArea(&monitor_4k, 32, 192, &wa) == TRUE);
        REQUIRE(wa.left == 0);
        REQUIRE(wa.top == 0);
        REQUIRE(wa.right == 3840);
        REQUIRE(wa.bottom == 2160 - 64); // 2096
    }

    SECTION("Invalid Arguments Handling") {
        RECT wa;
        REQUIRE(TE_CalculateWorkArea(nullptr, 32, 96, &wa) == FALSE);
        REQUIRE(TE_CalculateWorkArea(&monitor_1080p, 32, 96, nullptr) == FALSE);
    }
}
