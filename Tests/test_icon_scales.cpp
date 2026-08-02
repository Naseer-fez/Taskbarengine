#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Modules/icon_hover/magnification.h"
#include "../Modules/icon_hover/icon_layout.h"

TEST_CASE("End to End Pipeline", "[integration][layout]") {
    float centers[5] = {0.0f, 40.0f, 80.0f, 120.0f, 160.0f};
    float scales[5] = {0};
    float out_x[5] = {0};
    float out_y[5] = {0};
    
    // Compute scales
    TE_MagnifyComputeScales(80.0f, centers, scales, 5, 80.0f, 1.5f, TE_CURVE_LINEAR);
    
    // Compute layouts
    TE_LayoutComputePositions(scales, centers, out_x, out_y, 5, 32.0f, 1000.0f);
    
    // Pivot icon
    REQUIRE_THAT(out_x[2], Catch::Matchers::WithinAbs(80.0f, 0.001f));
    REQUIRE_THAT(out_y[2], Catch::Matchers::WithinAbs(1000.0f - 32.0f * 1.5f, 0.001f));
    
    // Icons to the left should move left
    REQUIRE(out_x[1] < centers[1]);
    REQUIRE(out_x[0] < centers[0]);
    
    // Icons to the right should move right
    REQUIRE(out_x[3] > centers[3]);
    REQUIRE(out_x[4] > centers[4]);
}
