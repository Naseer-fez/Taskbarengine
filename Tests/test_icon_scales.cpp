#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Modules/icon_hover/uia_discovery.h"
#include "../Modules/icon_hover/magnification.h"
#include <vector>

TEST_CASE("End-to-end scale calculation with mock icon layout", "[icon_scales][integration]") {
    std::vector<TE_TaskbarIcon> mock_icons(10);
    float start_x = 100.0f;
    float icon_spacing = 40.0f;
    float icon_size = 32.0f;

    for (int i = 0; i < 10; ++i) {
        mock_icons[i].bounds.left = (LONG)(start_x + i * icon_spacing);
        mock_icons[i].bounds.right = (LONG)(mock_icons[i].bounds.left + icon_size);
        mock_icons[i].bounds.top = 10;
        mock_icons[i].bounds.bottom = 42;
        mock_icons[i].icon_index = i;
    }

    std::vector<float> centers(10);
    for (int i = 0; i < 10; ++i) {
        centers[i] = (mock_icons[i].bounds.left + mock_icons[i].bounds.right) / 2.0f;
    }

    std::vector<float> scales(10, 1.0f);
    float cursor_x = centers[4]; /* Hover over icon 4 */
    float radius = 120.0f;
    float max_scale = 1.30f;

    TE_MagnifyComputeScales(cursor_x, centers.data(), scales.data(), 10, radius, max_scale, TE_CURVE_GAUSSIAN);

    /* Icon 4 is exact center */
    REQUIRE_THAT(scales[4], Catch::Matchers::WithinRel(1.30f, 0.001f));
    /* Icons 3 and 5 are equidistant neighbors */
    REQUIRE_THAT(scales[3], Catch::Matchers::WithinRel(scales[5], 0.001f));
    REQUIRE(scales[3] > 1.0f);
    REQUIRE(scales[3] < scales[4]);
}
