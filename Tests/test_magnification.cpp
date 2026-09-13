#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "magnification.h"
#include <vector>

using Catch::Matchers::WithinAbs;

TEST_CASE("Per-curve weight function tests", "[magnification]") {
    SECTION("weight(0) == 1.0") {
        REQUIRE(TE_MagnifyWeightGaussian(0.0f) == 1.0f);
        REQUIRE(TE_MagnifyWeightCubic(0.0f) == 1.0f);
        REQUIRE(TE_MagnifyWeightCosine(0.0f) == 1.0f);
        REQUIRE(TE_MagnifyWeightLinear(0.0f) == 1.0f);
    }

    SECTION("weight(1) for smooth curves") {
        REQUIRE(TE_MagnifyWeightCubic(1.0f) == 0.0f);
        REQUIRE(TE_MagnifyWeightCosine(1.0f) == 0.0f);
        REQUIRE(TE_MagnifyWeightLinear(1.0f) == 0.0f);
        REQUIRE_THAT(TE_MagnifyWeightGaussian(1.0f), WithinAbs(0.0439369f, 0.001f));
    }

    SECTION("Monotonically non-increasing") {
        float prev_g = 1.0f, prev_c = 1.0f, prev_cos = 1.0f, prev_l = 1.0f;
        for (int i = 1; i <= 10; ++i) {
            float u = i * 0.1f;
            float g = TE_MagnifyWeightGaussian(u);
            float c = TE_MagnifyWeightCubic(u);
            float cos = TE_MagnifyWeightCosine(u);
            float l = TE_MagnifyWeightLinear(u);
            
            REQUIRE(g <= prev_g);
            REQUIRE(c <= prev_c);
            REQUIRE(cos <= prev_cos);
            REQUIRE(l <= prev_l);
            
            prev_g = g;
            prev_c = c;
            prev_cos = cos;
            prev_l = l;
        }
    }

    SECTION("All values in [0, 1]") {
        for (int i = 0; i <= 100; ++i) {
            float u = i * 0.01f;
            float g = TE_MagnifyWeightGaussian(u);
            float c = TE_MagnifyWeightCubic(u);
            float cos = TE_MagnifyWeightCosine(u);
            float l = TE_MagnifyWeightLinear(u);
            
            REQUIRE((g >= 0.0f && g <= 1.0f));
            REQUIRE((c >= 0.0f && c <= 1.0f));
            REQUIRE((cos >= 0.0f && cos <= 1.0f));
            REQUIRE((l >= 0.0f && l <= 1.0f));
        }
    }

    SECTION("Negative u clamped") {
        REQUIRE(TE_MagnifyWeightGaussian(-0.5f) == 1.0f);
        REQUIRE(TE_MagnifyWeightCubic(-0.5f) == 1.0f);
        REQUIRE(TE_MagnifyWeightCosine(-0.5f) == 1.0f);
        REQUIRE(TE_MagnifyWeightLinear(-0.5f) == 1.0f);
    }

    SECTION("u > 1 returns 0") {
        REQUIRE(TE_MagnifyWeightGaussian(1.5f) == 0.0f);
        REQUIRE(TE_MagnifyWeightCubic(1.5f) == 0.0f);
        REQUIRE(TE_MagnifyWeightCosine(1.5f) == 0.0f);
        REQUIRE(TE_MagnifyWeightLinear(1.5f) == 0.0f);
    }
}

TEST_CASE("Batch compute scale tests", "[magnification]") {
    const int num_icons = 20;
    std::vector<float> icon_centers(num_icons);
    for (int i = 0; i < num_icons; ++i) {
        icon_centers[i] = 20.0f + i * 40.0f;
    }
    std::vector<float> scales(num_icons);
    float radius = 120.0f;
    float max_scale = 1.5f;

    TE_MagnifyCurveType curves[] = {
        TE_CURVE_GAUSSIAN, TE_CURVE_CUBIC, TE_CURVE_COSINE, TE_CURVE_LINEAR
    };

    SECTION("Scale bounds") {
        for (auto curve : curves) {
            TE_MagnifyComputeScales(400.0f, icon_centers.data(), scales.data(), num_icons, radius, max_scale, curve);
            for (float s : scales) {
                REQUIRE(s >= 1.0f);
                REQUIRE(s <= max_scale);
            }
        }
    }

    SECTION("Cursor on icon center") {
        for (auto curve : curves) {
            TE_MagnifyComputeScales(100.0f, icon_centers.data(), scales.data(), num_icons, radius, max_scale, curve);
            REQUIRE(scales[2] == max_scale);
        }
    }

    SECTION("Cursor far away") {
        for (auto curve : curves) {
            TE_MagnifyComputeScales(10000.0f, icon_centers.data(), scales.data(), num_icons, radius, max_scale, curve);
            for (float s : scales) {
                REQUIRE(s == 1.0f);
            }
        }
    }

    SECTION("Symmetry") {
        for (auto curve : curves) {
            // Cursor exactly between icon at idx 2 (100) and idx 3 (140) -> 120
            TE_MagnifyComputeScales(120.0f, icon_centers.data(), scales.data(), num_icons, radius, max_scale, curve);
            REQUIRE_THAT(scales[2], WithinAbs(scales[3], 0.0001f));
        }
    }

    SECTION("Zero count") {
        // Just verify no crash
        TE_MagnifyComputeScales(100.0f, nullptr, nullptr, 0, radius, max_scale, TE_CURVE_LINEAR);
        SUCCEED();
    }

    SECTION("Single icon") {
        float center = 50.0f;
        float scale = 0.0f;
        TE_MagnifyComputeScales(50.0f, &center, &scale, 1, radius, max_scale, TE_CURVE_LINEAR);
        REQUIRE(scale == max_scale);
    }

    SECTION("Zero radius") {
        for (auto curve : curves) {
            TE_MagnifyComputeScales(100.0f, icon_centers.data(), scales.data(), num_icons, 0.0f, max_scale, curve);
            for (float s : scales) {
                REQUIRE(s == 1.0f);
            }
        }
    }

    SECTION("max_scale = 1.0") {
        for (auto curve : curves) {
            TE_MagnifyComputeScales(100.0f, icon_centers.data(), scales.data(), num_icons, radius, 1.0f, curve);
            for (float s : scales) {
                REQUIRE(s == 1.0f);
            }
        }
    }

    SECTION("Curves produce distinct results") {
        std::vector<float> scales_g(num_icons), scales_c(num_icons), scales_cos(num_icons), scales_l(num_icons);
        TE_MagnifyComputeScales(110.0f, icon_centers.data(), scales_g.data(), num_icons, radius, max_scale, TE_CURVE_GAUSSIAN);
        TE_MagnifyComputeScales(110.0f, icon_centers.data(), scales_c.data(), num_icons, radius, max_scale, TE_CURVE_CUBIC);
        TE_MagnifyComputeScales(110.0f, icon_centers.data(), scales_cos.data(), num_icons, radius, max_scale, TE_CURVE_COSINE);
        TE_MagnifyComputeScales(110.0f, icon_centers.data(), scales_l.data(), num_icons, radius, max_scale, TE_CURVE_LINEAR);
        
        // Icon at idx 3 (center 140) is distance 30 from cursor 110
        // u = 30 / 120 = 0.25
        REQUIRE(scales_g[3] != scales_c[3]);
        REQUIRE(scales_c[3] != scales_cos[3]);
        REQUIRE(scales_cos[3] != scales_l[3]);
    }
}
