#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sdk/te_easing.h>

TEST_CASE("Easing Functions - Boundaries", "[easing]") {
    SECTION("Linear") {
        REQUIRE_THAT(TE_EasingApply(0.0f, TE_EASE_LINEAR), Catch::Matchers::WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(TE_EasingApply(1.0f, TE_EASE_LINEAR), Catch::Matchers::WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(TE_EasingApply(0.5f, TE_EASE_LINEAR), Catch::Matchers::WithinAbs(0.5f, 0.001f));
    }
    SECTION("In Cubic") {
        REQUIRE_THAT(TE_EasingApply(0.0f, TE_EASE_IN_CUBIC), Catch::Matchers::WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(TE_EasingApply(1.0f, TE_EASE_IN_CUBIC), Catch::Matchers::WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(TE_EasingApply(0.5f, TE_EASE_IN_CUBIC), Catch::Matchers::WithinAbs(0.125f, 0.001f));
    }
    SECTION("Out Cubic") {
        REQUIRE_THAT(TE_EasingApply(0.0f, TE_EASE_OUT_CUBIC), Catch::Matchers::WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(TE_EasingApply(1.0f, TE_EASE_OUT_CUBIC), Catch::Matchers::WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(TE_EasingApply(0.5f, TE_EASE_OUT_CUBIC), Catch::Matchers::WithinAbs(0.875f, 0.001f));
    }
}

TEST_CASE("Easing Functions - Clamping", "[easing]") {
    REQUIRE_THAT(TE_EasingApply(-0.5f, TE_EASE_OUT_CUBIC), Catch::Matchers::WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(TE_EasingApply(1.5f, TE_EASE_OUT_CUBIC), Catch::Matchers::WithinAbs(1.0f, 0.001f));
}

TEST_CASE("Lerp", "[easing]") {
    REQUIRE_THAT(TE_Lerp(10.0f, 20.0f, 0.0f), Catch::Matchers::WithinAbs(10.0f, 0.001f));
    REQUIRE_THAT(TE_Lerp(10.0f, 20.0f, 1.0f), Catch::Matchers::WithinAbs(20.0f, 0.001f));
    REQUIRE_THAT(TE_Lerp(10.0f, 20.0f, 0.5f), Catch::Matchers::WithinAbs(15.0f, 0.001f));
    
    REQUIRE_THAT(TE_LerpEased(10.0f, 20.0f, 0.5f, TE_EASE_IN_CUBIC), Catch::Matchers::WithinAbs(11.25f, 0.001f));
}
