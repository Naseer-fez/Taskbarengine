#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <app/tray.h>
}

TEST_CASE("Tray app constants and API validation", "[tray]") {
    // 1. Validate constants
    REQUIRE(WM_TRAYICON == WM_USER + 1);
    REQUIRE(IDI_APPICON == 101);

    // 2. Validate TE_TrayDestroy returns S_OK when no tray is created
    HRESULT hr = TE_TrayDestroy();
    REQUIRE(hr == S_OK);

    // 3. Validate TE_TrayCreate handles invalid parent window context (null parent hwnd)
    // Shell_NotifyIconW will fail, yielding an HRESULT error from GetLastError()
    hr = TE_TrayCreate(NULL, NULL);
    REQUIRE(FAILED(hr));
}
