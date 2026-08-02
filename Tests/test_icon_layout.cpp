#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Modules/icon_hover/icon_layout.h"

TEST_CASE("Icon Layout - Identity", "[layout]") {
    float scales[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float base_x[5] = {0.0f, 40.0f, 80.0f, 120.0f, 160.0f};
    float out_x[5] = {0};
    float out_y[5] = {0};

    TE_LayoutComputePositions(scales, base_x, out_x, out_y, 5, 32.0f, 1000.0f);

    for (int i = 0; i < 5; i++) {
        REQUIRE_THAT(out_x[i], Catch::Matchers::WithinAbs(base_x[i], 0.001f));
        REQUIRE_THAT(out_y[i], Catch::Matchers::WithinAbs(1000.0f - 32.0f, 0.001f));
    }
}

TEST_CASE("Icon Layout - Spread", "[layout]") {
    float scales[3] = {1.2f, 1.5f, 1.2f};
    float base_x[3] = {0.0f, 40.0f, 80.0f};
    float out_x[3] = {0};
    float out_y[3] = {0};

    TE_LayoutComputePositions(scales, base_x, out_x, out_y, 3, 32.0f, 1000.0f);

    // Center icon (pivot) should not move horizontally
    REQUIRE_THAT(out_x[1], Catch::Matchers::WithinAbs(40.0f, 0.001f));
    REQUIRE_THAT(out_y[1], Catch::Matchers::WithinAbs(1000.0f - 32.0f * 1.5f, 0.001f));

    // Left icon should move left
    REQUIRE(out_x[0] < base_x[0]);
    // Right icon should move right
    REQUIRE(out_x[2] > base_x[2]);
    
    // Spread should be symmetric
    REQUIRE_THAT(out_x[1] - out_x[0], Catch::Matchers::WithinAbs(out_x[2] - out_x[1], 0.001f));
}
