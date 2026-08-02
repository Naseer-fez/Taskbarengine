#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Modules/icon_hover/magnification.h"

TEST_CASE("Magnification Math - Curves", "[magnification]") {
    float max_scale = 2.0f;
    float radius = 100.0f;

    SECTION("Linear Curve") {
        REQUIRE_THAT(TE_MagnifyScale(0.0f, radius, max_scale, TE_CURVE_LINEAR), Catch::Matchers::WithinAbs(2.0f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(50.0f, radius, max_scale, TE_CURVE_LINEAR), Catch::Matchers::WithinAbs(1.5f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(100.0f, radius, max_scale, TE_CURVE_LINEAR), Catch::Matchers::WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(150.0f, radius, max_scale, TE_CURVE_LINEAR), Catch::Matchers::WithinAbs(1.0f, 0.001f));
    }

    SECTION("Cubic Curve") {
        REQUIRE_THAT(TE_MagnifyScale(0.0f, radius, max_scale, TE_CURVE_CUBIC), Catch::Matchers::WithinAbs(2.0f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(50.0f, radius, max_scale, TE_CURVE_CUBIC), Catch::Matchers::WithinAbs(1.125f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(100.0f, radius, max_scale, TE_CURVE_CUBIC), Catch::Matchers::WithinAbs(1.0f, 0.001f));
    }

    SECTION("Cosine Curve") {
        REQUIRE_THAT(TE_MagnifyScale(0.0f, radius, max_scale, TE_CURVE_COSINE), Catch::Matchers::WithinAbs(2.0f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(50.0f, radius, max_scale, TE_CURVE_COSINE), Catch::Matchers::WithinAbs(1.5f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(100.0f, radius, max_scale, TE_CURVE_COSINE), Catch::Matchers::WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(-50.0f, radius, max_scale, TE_CURVE_COSINE), Catch::Matchers::WithinAbs(1.5f, 0.001f));
    }

    SECTION("Gaussian Curve") {
        REQUIRE_THAT(TE_MagnifyScale(0.0f, radius, max_scale, TE_CURVE_GAUSSIAN), Catch::Matchers::WithinAbs(2.0f, 0.001f));
        REQUIRE(TE_MagnifyScale(50.0f, radius, max_scale, TE_CURVE_GAUSSIAN) < 1.5f);
        REQUIRE(TE_MagnifyScale(50.0f, radius, max_scale, TE_CURVE_GAUSSIAN) > 1.0f);
        REQUIRE_THAT(TE_MagnifyScale(100.0f, radius, max_scale, TE_CURVE_GAUSSIAN), Catch::Matchers::WithinAbs(1.0f, 0.05f));
        REQUIRE_THAT(TE_MagnifyScale(-50.0f, radius, max_scale, TE_CURVE_GAUSSIAN), 
                     Catch::Matchers::WithinAbs(TE_MagnifyScale(50.0f, radius, max_scale, TE_CURVE_GAUSSIAN), 0.001f));
    }
}

TEST_CASE("Magnification Math - Batch Compute", "[magnification]") {
    float centers[5] = {0.0f, 40.0f, 80.0f, 120.0f, 160.0f};
    float scales[5] = {0};
    
    TE_MagnifyComputeScales(80.0f, centers, scales, 5, 80.0f, 1.5f, TE_CURVE_LINEAR);
    
    // Center icon at 80.0 is exactly at cursor
    REQUIRE_THAT(scales[2], Catch::Matchers::WithinAbs(1.5f, 0.001f));
    
    // Icon at 40 and 120 are 40px away (half radius)
    REQUIRE_THAT(scales[1], Catch::Matchers::WithinAbs(1.25f, 0.001f));
    REQUIRE_THAT(scales[3], Catch::Matchers::WithinAbs(1.25f, 0.001f));
    
    // Icon at 0 and 160 are 80px away (full radius)
    REQUIRE_THAT(scales[0], Catch::Matchers::WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(scales[4], Catch::Matchers::WithinAbs(1.0f, 0.001f));
}
