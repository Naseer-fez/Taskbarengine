#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <taskbar_resize.h>
}

TEST_CASE("TaskbarResize clamps configured height", "[taskbar_resize]") {
    REQUIRE(TE_TaskbarResizeClampHeight(12) == TE_TASKBAR_RESIZE_MIN_HEIGHT);
    REQUIRE(TE_TaskbarResizeClampHeight(36) == 36);
    REQUIRE(TE_TaskbarResizeClampHeight(120) == TE_TASKBAR_RESIZE_MAX_HEIGHT);
}

TEST_CASE("TaskbarResize scales logical height for DPI", "[taskbar_resize]") {
    REQUIRE(TE_TaskbarResizeScaleForDpi(40, 96) == 40);
    REQUIRE(TE_TaskbarResizeScaleForDpi(40, 144) == 60);
    REQUIRE(TE_TaskbarResizeScaleForDpi(40, 0) == 40);
}

TEST_CASE("TaskbarResize enforces height in WINDOWPOS struct", "[taskbar_resize]") {
    WINDOWPOS wp{};
    wp.x = 0;
    wp.y = 1000;
    wp.cx = 1920;
    wp.cy = 48;

    /* Enforce height = 36 at 96 DPI (36px). Old cy was 48, new cy is 36, y moves down by 12 */
    TE_TaskbarResizeEnforceWindowPos(&wp, 36, 96);
    REQUIRE(wp.cy == 36);
    REQUIRE(wp.y == 1012);

    /* Enforce height = 40 at 144 DPI (60px). Old cy is now 36, new cy is 60, y moves up by 24 */
    TE_TaskbarResizeEnforceWindowPos(&wp, 40, 144);
    REQUIRE(wp.cy == 60);
    REQUIRE(wp.y == 988);

    /* Edge case: Null pointer check should be handled safely */
    TE_TaskbarResizeEnforceWindowPos(nullptr, 40, 96);
}
