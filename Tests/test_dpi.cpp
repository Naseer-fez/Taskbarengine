#include <catch2/catch_test_macros.hpp>
#include <sdk/te_dpi.h>

TEST_CASE("TE_ScaleDPI scaling calculation", "[dpi]") {
    SECTION("100% scaling (96 DPI)") {
        REQUIRE(TE_ScaleDPI(48, 96) == 48);
        REQUIRE(TE_ScaleDPI(32, 96) == 32);
        REQUIRE(TE_ScaleDPI(0, 96) == 0);
    }

    SECTION("150% scaling (144 DPI)") {
        REQUIRE(TE_ScaleDPI(48, 144) == 72);
        REQUIRE(TE_ScaleDPI(32, 144) == 48);
    }

    SECTION("200% scaling (192 DPI)") {
        REQUIRE(TE_ScaleDPI(48, 192) == 96);
        REQUIRE(TE_ScaleDPI(32, 192) == 64);
    }

    SECTION("Rounding behavior") {
        /* 1 * 144 = 144, 144 / 96 = 1.5 -> rounds to 2 */
        REQUIRE(TE_ScaleDPI(1, 144) == 2);
    }
}
