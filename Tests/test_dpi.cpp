#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <sdk/te_dpi.h>
}

TEST_CASE("DPI scaling", "[dpi]") {
    SECTION("Identity at 96 DPI") {
        CHECK(TE_ScaleDPI(48, 96) == 48);
        CHECK(TE_ScaleDPI(100, 96) == 100);
        CHECK(TE_ScaleDPI(0, 96) == 0);
    }

    SECTION("Scale up at 120 DPI (125%)") {
        CHECK(TE_ScaleDPI(48, 120) == 60);
        CHECK(TE_ScaleDPI(96, 120) == 120);
    }

    SECTION("Scale up at 144 DPI (150%)") {
        CHECK(TE_ScaleDPI(48, 144) == 72);
        CHECK(TE_ScaleDPI(96, 144) == 144);
    }

    SECTION("Scale up at 192 DPI (200%)") {
        CHECK(TE_ScaleDPI(48, 192) == 96);
        CHECK(TE_ScaleDPI(96, 192) == 192);
    }

    SECTION("DPI of zero returns zero") {
        CHECK(TE_ScaleDPI(48, 0) == 0);
        CHECK(TE_ScaleDPI(0, 0) == 0);
    }

    SECTION("Negative value") {
        CHECK(TE_ScaleDPI(-48, 192) == -96);
    }
}

TEST_CASE("DPI unscaling", "[dpi]") {
    SECTION("Round-trip at 144 DPI") {
        int scaled = TE_ScaleDPI(48, 144);
        CHECK(TE_UnscaleDPI(scaled, 144) == 48);
    }

    SECTION("Identity at 96 DPI") {
        CHECK(TE_UnscaleDPI(48, 96) == 48);
    }

    SECTION("DPI of zero returns zero") {
        CHECK(TE_UnscaleDPI(48, 0) == 0);
    }
}
