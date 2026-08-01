#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Modules/icon_hover/magnification.h"
#include <vector>

TEST_CASE("Magnification curve math", "[magnification]") {
    float radius = 120.0f;
    float max_scale = 1.30f;

    SECTION("Gaussian curve properties") {
        /* scale at d=0 is max_scale */
        REQUIRE_THAT(TE_MagnifyScale(0.0f, radius, max_scale, TE_CURVE_GAUSSIAN), Catch::Matchers::WithinRel(1.30f, 0.001f));
        /* symmetry */
        REQUIRE(TE_MagnifyScale(-50.0f, radius, max_scale, TE_CURVE_GAUSSIAN) == TE_MagnifyScale(50.0f, radius, max_scale, TE_CURVE_GAUSSIAN));
        /* monotonic decrease */
        REQUIRE(TE_MagnifyScale(30.0f, radius, max_scale, TE_CURVE_GAUSSIAN) > TE_MagnifyScale(60.0f, radius, max_scale, TE_CURVE_GAUSSIAN));
    }

    SECTION("Cosine curve properties") {
        REQUIRE_THAT(TE_MagnifyScale(0.0f, radius, max_scale, TE_CURVE_COSINE), Catch::Matchers::WithinRel(1.30f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(radius, radius, max_scale, TE_CURVE_COSINE), Catch::Matchers::WithinRel(1.0f, 0.001f));
        REQUIRE(TE_MagnifyScale(radius + 10.0f, radius, max_scale, TE_CURVE_COSINE) == 1.0f);
        REQUIRE(TE_MagnifyScale(-50.0f, radius, max_scale, TE_CURVE_COSINE) == TE_MagnifyScale(50.0f, radius, max_scale, TE_CURVE_COSINE));
    }

    SECTION("Linear curve properties") {
        REQUIRE_THAT(TE_MagnifyScale(0.0f, radius, max_scale, TE_CURVE_LINEAR), Catch::Matchers::WithinRel(1.30f, 0.001f));
        REQUIRE_THAT(TE_MagnifyScale(60.0f, radius, max_scale, TE_CURVE_LINEAR), Catch::Matchers::WithinRel(1.15f, 0.001f));
        REQUIRE(TE_MagnifyScale(radius, radius, max_scale, TE_CURVE_LINEAR) == 1.0f);
    }

    SECTION("Cubic curve properties") {
        REQUIRE_THAT(TE_MagnifyScale(0.0f, radius, max_scale, TE_CURVE_CUBIC), Catch::Matchers::WithinRel(1.30f, 0.001f));
        REQUIRE(TE_MagnifyScale(radius, radius, max_scale, TE_CURVE_CUBIC) == 1.0f);
    }

    SECTION("Batch scale computation") {
        std::vector<float> centers = { 10.0f, 50.0f, 90.0f, 130.0f, 170.0f };
        std::vector<float> scales(centers.size(), 1.0f);

        TE_MagnifyComputeScales(90.0f, centers.data(), scales.data(), (int)centers.size(), radius, max_scale, TE_CURVE_GAUSSIAN);

        /* Center icon at index 2 (x=90) should have maximum scale */
        REQUIRE_THAT(scales[2], Catch::Matchers::WithinRel(1.30f, 0.001f));
        /* Neighbor icons 1 and 3 should be equal and > 1.0 */
        REQUIRE(scales[1] == scales[3]);
        REQUIRE(scales[1] > 1.0f);
        REQUIRE(scales[1] < scales[2]);
    }
}
