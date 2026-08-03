#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <app/tray_menu.h>
}

TEST_CASE("Tray menu constants and command handling logic", "[tray_menu]") {
    // 1. Verify constants
    REQUIRE(TE_TRAY_MENU_SETTINGS == 40001);
    REQUIRE(TE_TRAY_MENU_ENABLE_ALL == 40002);
    REQUIRE(TE_TRAY_MENU_RELOAD_CONFIG == 40003);
    REQUIRE(TE_TRAY_MENU_ABOUT == 40004);
    REQUIRE(TE_TRAY_MENU_EXIT == 40005);

    // 2. Verify all constants are unique
    REQUIRE(TE_TRAY_MENU_SETTINGS != TE_TRAY_MENU_ENABLE_ALL);
    REQUIRE(TE_TRAY_MENU_SETTINGS != TE_TRAY_MENU_RELOAD_CONFIG);
    REQUIRE(TE_TRAY_MENU_SETTINGS != TE_TRAY_MENU_ABOUT);
    REQUIRE(TE_TRAY_MENU_SETTINGS != TE_TRAY_MENU_EXIT);

    REQUIRE(TE_TRAY_MENU_ENABLE_ALL != TE_TRAY_MENU_RELOAD_CONFIG);
    REQUIRE(TE_TRAY_MENU_ENABLE_ALL != TE_TRAY_MENU_ABOUT);
    REQUIRE(TE_TRAY_MENU_ENABLE_ALL != TE_TRAY_MENU_EXIT);

    REQUIRE(TE_TRAY_MENU_RELOAD_CONFIG != TE_TRAY_MENU_ABOUT);
    REQUIRE(TE_TRAY_MENU_RELOAD_CONFIG != TE_TRAY_MENU_EXIT);

    REQUIRE(TE_TRAY_MENU_ABOUT != TE_TRAY_MENU_EXIT);

    // 3. Verify handling of invalid/unrecognized command codes returns false
    // Since it doesn't match any enum, it goes to default and returns false immediately without performing window actions.
    bool handled = TE_TrayMenuHandleCommand(NULL, 0);
    REQUIRE_FALSE(handled);

    handled = TE_TrayMenuHandleCommand(NULL, 99999);
    REQUIRE_FALSE(handled);
}
