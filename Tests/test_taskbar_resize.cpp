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
