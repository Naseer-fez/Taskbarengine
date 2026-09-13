#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <windows.h>
#include <sdk/te_types.h>
#include <sdk/te_events.h>
#include <sdk/te_plugin.h>
#include "magnification.h"
#include "uia_discovery.h"
#include "dcomp_overlay.h"
#include "frame_loop.h"
#include "icon_hover_internal.h"
#include <vector>
#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("Icon Hover - Mouse Event Structure & Dispatch", "[icon_hover][mouse]") {
    TE_TaskbarMouseData data;
    data.cursor_pos.x = 250;
    data.cursor_pos.y = 1050;
    data.is_in_taskbar = TRUE;

    REQUIRE(data.cursor_pos.x == 250);
    REQUIRE(data.cursor_pos.y == 1050);
    REQUIRE(data.is_in_taskbar == TRUE);

    data.is_in_taskbar = FALSE;
    REQUIRE(data.is_in_taskbar == FALSE);
}

TEST_CASE("Icon Hover - High-DPI Headroom and Radius Scaling", "[icon_hover][dpi]") {
    auto calc_headroom = [](uint32_t dpi) -> int {
        return (int)(64.0f * (float)dpi / 96.0f);
    };

    SECTION("Standard 96 DPI (100%)") {
        REQUIRE(calc_headroom(96) == 64);
    }

    SECTION("120 DPI (125%)") {
        REQUIRE(calc_headroom(120) == 80);
    }

    SECTION("144 DPI (150%)") {
        REQUIRE(calc_headroom(144) == 96);
    }

    SECTION("192 DPI (200%)") {
        REQUIRE(calc_headroom(192) == 128);
    }
}

TEST_CASE("Icon Hover - Displaced Position Math & Top Headroom Expansion", "[icon_hover][displacement]") {
    const int count = 5;
    float base_w = 48.0f;
    float base_h = 48.0f;
    float scales[count] = { 1.0f, 1.2f, 1.4f, 1.2f, 1.0f };

    float pos_x[count] = { 0 };
    float pos_y[count] = { 0 };

    float cumulative_dx = 0.0f;
    for (int i = 0; i < count; i++) {
        float scale = scales[i];
        float extra_w = base_w * (scale - 1.0f);
        pos_x[i] = cumulative_dx - extra_w / 2.0f;

        float extra_h = base_h * (scale - 1.0f);
        pos_y[i] = -extra_h; // Grow upward

        cumulative_dx += extra_w;
    }

    SECTION("Unscaled icons have zero displacement") {
        REQUIRE(scales[0] == 1.0f);
        REQUIRE(pos_y[0] == 0.0f);
    }

    SECTION("Magnified peak icon grows upward without exceeding headroom") {
        REQUIRE_THAT(pos_y[2], WithinAbs(-19.2f, 0.01f));
        REQUIRE(fabsf(pos_y[2]) < 64.0f);
    }

    SECTION("Cumulative X displacement pushes subsequent icons rightward") {
        REQUIRE(pos_x[4] > pos_x[0]);
        REQUIRE(pos_x[3] > pos_x[1]);
    }
}

TEST_CASE("Icon Hover - Alpha Channel Preservation for 32-bit DIB", "[icon_hover][alpha]") {
    const int w = 16, h = 16;
    std::vector<uint32_t> pixels(w * h, 0x00A0B0C0);

    for (auto p : pixels) {
        REQUIRE((p & 0xFF000000) == 0);
    }

    BOOL has_alpha = FALSE;
    for (auto p : pixels) {
        if ((p & 0xFF000000) != 0) {
            has_alpha = TRUE;
            break;
        }
    }
    if (!has_alpha) {
        for (auto& p : pixels) {
            if ((p & 0x00FFFFFF) != 0) {
                p |= 0xFF000000;
            }
        }
    }

    for (auto p : pixels) {
        REQUIRE((p & 0xFF000000) == 0xFF000000);
    }
}

TEST_CASE("Icon Hover - Settle Animation Convergence", "[icon_hover][settle]") {
    float current_scale = 1.4f;
    const float target_scale = 1.0f;
    const int speed_ms = 150;
    const float dt = 0.008f;

    float lerp_speed = (1000.0f / (float)speed_ms) * dt;
    int ticks = 0;
    while (fabsf(current_scale - target_scale) >= 0.001f && ticks < 200) {
        current_scale += (target_scale - current_scale) * lerp_speed;
        ticks++;
    }

    REQUIRE_THAT(current_scale, WithinAbs(1.0f, 0.001f));
    REQUIRE(ticks <= 120);
}


TEST_CASE("Icon Hover - Inertial Bounce Spring Oscillator Convergence", "[icon_hover][bounce]") {
    float currentOffsetY = 0.0f;
    const float targetOffsetY = 0.0f;
    float velocityOffsetY = 0.0f;

    // Apply upward impulse
    const float impulseStrength = 600.0f;
    velocityOffsetY = -impulseStrength; // Upward instantaneous velocity

    const float k_spring_y = 200.0f;
    const float c_damping_y = 28.28f; // 2 * sqrt(200.0)
    const float dt = 0.008f; // ~125Hz tick

    float peak_upward = 0.0f;
    int ticks = 0;

    while ((fabsf(currentOffsetY - targetOffsetY) > 0.5f || fabsf(velocityOffsetY) > 1.0f) && ticks < 500) {
        float forceOffsetY = -k_spring_y * (currentOffsetY - targetOffsetY) - c_damping_y * velocityOffsetY;
        velocityOffsetY += forceOffsetY * dt;
        currentOffsetY += velocityOffsetY * dt;

        if (currentOffsetY < peak_upward) {
            peak_upward = currentOffsetY;
        }

        if (currentOffsetY > 0.0f) {
            currentOffsetY = 0.0f;
            velocityOffsetY = 0.0f;
        }

        ticks++;
    }

    // Peak displacement should be upward (negative Y)
    REQUIRE(peak_upward < -10.0f);
    // Peak should not exceed headroom
    REQUIRE(fabsf(peak_upward) < 1024.0f);
    // Settles back to 0.0f within reasonable time (~150 ticks = ~1.2s)
    REQUIRE(ticks < 200);
    REQUIRE(fabsf(currentOffsetY - targetOffsetY) <= 0.5f);
    REQUIRE(fabsf(velocityOffsetY) <= 1.0f);
}

TEST_CASE("Icon Hover - Inertial Bounce Headroom Clamping", "[icon_hover][bounce][headroom]") {
    float currentOffsetY = 0.0f;
    const float targetOffsetY = 0.0f;
    const float max_headroom = 1024.0f; // TE_HOVER_HEADROOM_BASE_PX

    // Extremely large impulse that would otherwise launch the icon thousands of pixels
    float velocityOffsetY = -100000.0f;
    const float k_spring_y = 200.0f;
    const float c_damping_y = 28.28f;
    const float dt = 0.008f;

    for (int i = 0; i < 50; i++) {
        float forceOffsetY = -k_spring_y * (currentOffsetY - targetOffsetY) - c_damping_y * velocityOffsetY;
        velocityOffsetY += forceOffsetY * dt;
        currentOffsetY += velocityOffsetY * dt;

        // Honor headroom constraint
        if (currentOffsetY < -max_headroom) {
            currentOffsetY = -max_headroom;
            if (velocityOffsetY < 0.0f) {
                velocityOffsetY = 0.0f;
            }
        }

        // Clamp floor
        if (currentOffsetY > 0.0f) {
            currentOffsetY = 0.0f;
            velocityOffsetY = 0.0f;
        }

        REQUIRE(currentOffsetY >= -max_headroom);
        REQUIRE(currentOffsetY <= 0.0f);
    }
}

TEST_CASE("Icon Hover - Notification Flash Event Codes", "[icon_hover][flash]") {
    #ifndef HSHELL_REDRAW
    #define HSHELL_REDRAW 6
    #endif
    #ifndef HSHELL_FLASH
    #define HSHELL_FLASH (HSHELL_REDRAW | 0x8000)
    #endif

    REQUIRE(HSHELL_REDRAW == 6);
    REQUIRE(HSHELL_FLASH == (6 | 0x8000));
    REQUIRE((HSHELL_FLASH & 0x8000) != 0);
    REQUIRE((HSHELL_FLASH & 0x7FFF) == HSHELL_REDRAW);
}

TEST_CASE("Icon Hover - Application Identifier Substring Matching", "[icon_hover][matching]") {
    auto StrCaseContains = [](const wchar_t* haystack, const wchar_t* needle) -> bool {
        if (!haystack || !needle || !*haystack || !*needle) return false;
        size_t h_len = wcslen(haystack);
        size_t n_len = wcslen(needle);
        if (n_len > h_len) return false;
        for (size_t i = 0; i <= h_len - n_len; i++) {
            if (_wcsnicmp(haystack + i, needle, n_len) == 0) return true;
        }
        return false;
    };

    const wchar_t* app_id1 = L"AppID: C:\\Program Files\\Notepad++\\notepad++.exe";
    const wchar_t* app_id2 = L"AppID: Microsoft.WindowsNotepad_8wekyb3d8bbwe!App";
    const wchar_t* app_id3 = L"AppID: Google.Chrome";

    REQUIRE(StrCaseContains(app_id1, L"notepad++"));
    REQUIRE(StrCaseContains(app_id1, L"notepad++.exe"));
    REQUIRE(!StrCaseContains(app_id1, L"chrome"));

    REQUIRE(StrCaseContains(app_id2, L"Microsoft.WindowsNotepad"));
    REQUIRE(StrCaseContains(app_id2, L"8wekyb3d8bbwe"));

    REQUIRE(StrCaseContains(app_id3, L"chrome"));
    REQUIRE(StrCaseContains(app_id3, L"Google.Chrome"));
}

TEST_CASE("Icon Hover - Overlay Topmost Z-Order Detection and Recovery", "[icon_hover][zorder]") {
    auto IsTaskbarAboveOverlay = [](HWND overlay_hwnd, HWND taskbar_hwnd) -> bool {
        if (!overlay_hwnd || !IsWindow(overlay_hwnd)) return false;
        HWND prev = GetWindow(overlay_hwnd, GW_HWNDPREV);
        if (!prev) return false;

        for (HWND w = prev; w != NULL; w = GetWindow(w, GW_HWNDPREV)) {
            if (taskbar_hwnd && (w == taskbar_hwnd || GetAncestor(w, GA_ROOT) == taskbar_hwnd)) {
                return true;
            }

            WCHAR className[64] = { 0 };
            if (GetClassNameW(w, className, 64) > 0) {
                if (_wcsicmp(className, L"Shell_TrayWnd") == 0 ||
                    _wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0 ||
                    _wcsicmp(className, L"TopLevelWindowForOverflowXamlIsland") == 0) {
                    return true;
                }
            }
        }
        return false;
    };

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"TE_ZOrderTestClass";
    RegisterClassW(&wc);

    HWND hwndTaskbar = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"TE_ZOrderTestClass",
        L"SimulatedTaskbar",
        WS_POPUP,
        0, 1000, 1920, 80,
        NULL, NULL, GetModuleHandleW(NULL), NULL
    );
    REQUIRE(hwndTaskbar != NULL);

    HWND hwndOverlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"TE_ZOrderTestClass",
        L"SimulatedOverlay",
        WS_POPUP,
        0, 950, 1920, 130,
        NULL, NULL, GetModuleHandleW(NULL), NULL
    );
    REQUIRE(hwndOverlay != NULL);

    ShowWindow(hwndTaskbar, SW_SHOWNA);
    ShowWindow(hwndOverlay, SW_SHOWNA);

    // Overlay is brought to HWND_TOPMOST
    SetWindowPos(hwndOverlay, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    CHECK(!IsTaskbarAboveOverlay(hwndOverlay, hwndTaskbar));

    // Simulate user clicking on taskbar: Explorer calls SetWindowPos(taskbar, HWND_TOPMOST, ...)
    SetWindowPos(hwndTaskbar, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Verify taskbar is now detected as being ABOVE overlay
    CHECK(IsTaskbarAboveOverlay(hwndOverlay, hwndTaskbar));

    // Recovery: re-assert HWND_TOPMOST on overlay
    SetWindowPos(hwndOverlay, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Verify overlay is now back on top
    CHECK(!IsTaskbarAboveOverlay(hwndOverlay, hwndTaskbar));

    DestroyWindow(hwndOverlay);
    DestroyWindow(hwndTaskbar);
    UnregisterClassW(L"TE_ZOrderTestClass", GetModuleHandleW(NULL));
}

TEST_CASE("Icon Hover - 3D Tilt Matrix Calculation & Perspective Projection", "[icon_hover][3d][matrix]") {
    struct Mat4 {
        float m[4][4];
    };

    auto MatrixMultiply = [](const Mat4& a, const Mat4& b) -> Mat4 {
        Mat4 out = {};
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                out.m[r][c] = a.m[r][0] * b.m[0][c] +
                              a.m[r][1] * b.m[1][c] +
                              a.m[r][2] * b.m[2][c] +
                              a.m[r][3] * b.m[3][c];
            }
        }
        return out;
    };

    SECTION("Identity perspective produces non-zero depth projection element") {
        float d = 1000.0f;
        Mat4 M_persp = {};
        for (int i = 0; i < 4; i++) M_persp.m[i][i] = 1.0f;
        M_persp.m[2][3] = -1.0f / d;

        REQUIRE_THAT(M_persp.m[2][3], WithinAbs(-0.001f, 0.00001f));
    }

    SECTION("Pitch and Yaw rotations produce expected trigonometric components") {
        float angle = 0.35f; // ~20 degrees
        float cosA = cosf(angle);
        float sinA = sinf(angle);

        Mat4 Rx = {};
        for (int i = 0; i < 4; i++) Rx.m[i][i] = 1.0f;
        Rx.m[1][1] = cosA;
        Rx.m[1][2] = -sinA;
        Rx.m[2][1] = sinA;
        Rx.m[2][2] = cosA;

        Mat4 Ry = {};
        for (int i = 0; i < 4; i++) Ry.m[i][i] = 1.0f;
        Ry.m[0][0] = cosA;
        Ry.m[0][2] = sinA;
        Ry.m[2][0] = -sinA;
        Ry.m[2][2] = cosA;

        Mat4 Rxy = MatrixMultiply(Rx, Ry);
        REQUIRE_THAT(Rxy.m[0][0], WithinAbs(cosA, 0.001f));
        REQUIRE_THAT(Rxy.m[1][1], WithinAbs(cosA, 0.001f));
    }
}

TEST_CASE("Icon Hover - Drag-and-Drop Drop Zone Displacement & Recession", "[icon_hover][drag][displacement]") {
    const int count = 5;
    float icon_centers[count] = { 50.0f, 100.0f, 150.0f, 200.0f, 250.0f };
    float cursor_x = 150.0f; // Directly hovering icon index 2
    float radius = 120.0f;

    auto ComputeDisplacement = [&](bool is_dragging, float drop_zone_push, float* out_x) {
        float push_factor = is_dragging ? drop_zone_push : 15.0f;
        for (int i = 0; i < count; i++) {
            float dx = icon_centers[i] - cursor_x;
            float dist = fabsf(dx);
            if (dist < radius && dist > 0.5f) {
                float falloff = 1.0f - (dist / radius);
                float sign = (dx > 0.0f) ? 1.0f : -1.0f;
                if (is_dragging && dist < 15.0f) {
                    out_x[i] = 0.0f;
                } else {
                    out_x[i] = sign * push_factor * falloff;
                }
            } else {
                out_x[i] = 0.0f;
            }
        }
    };

    float normal_pos_x[count] = {};
    ComputeDisplacement(false, 50.0f, normal_pos_x);

    float drag_pos_x[count] = {};
    ComputeDisplacement(true, 50.0f, drag_pos_x);

    SECTION("Hovered icon itself has zero horizontal push during drag") {
        REQUIRE(drag_pos_x[2] == 0.0f);
    }

    SECTION("Neighbor icons part significantly wider during drag") {
        REQUIRE(drag_pos_x[1] < normal_pos_x[1]);
        REQUIRE(drag_pos_x[1] < -20.0f);
        REQUIRE(drag_pos_x[3] > normal_pos_x[3]);
        REQUIRE(drag_pos_x[3] > 20.0f);

        float normal_gap = (icon_centers[3] + normal_pos_x[3]) - (icon_centers[1] + normal_pos_x[1]);
        float drag_gap = (icon_centers[3] + drag_pos_x[3]) - (icon_centers[1] + drag_pos_x[1]);
        REQUIRE(drag_gap > normal_gap + 40.0f);
    }

    SECTION("Custom drop zone push increases separation corridor") {
        float custom_drag_pos_x[count] = {};
        ComputeDisplacement(true, 80.0f, custom_drag_pos_x);
        REQUIRE(custom_drag_pos_x[1] < drag_pos_x[1]);
        REQUIRE(custom_drag_pos_x[3] > drag_pos_x[3]);
    }
}

TEST_CASE("Icon Hover - Configurable 3D Tilt & Drag-Drop Physics", "[icon_hover][config][3d][drag]") {
    SECTION("Disabled tilt results in zero tilt targets regardless of mouse proximity") {
        int tilt_enabled = 0;
        float max_tilt_deg = 20.0f;
        const float deg2rad = 3.14159265f / 180.0f;
        const float max_tilt_rad = tilt_enabled ? (max_tilt_deg * deg2rad) : 0.0f;

        float cursor_x = 100.0f, cursor_y = 100.0f;
        float center_x = 90.0f, center_y = 90.0f;
        float radius = 120.0f;
        float dx = cursor_x - center_x;
        float dy = cursor_y - center_y;
        float dist = sqrtf(dx * dx + dy * dy);

        float target_tilt_x = 0.0f;
        float target_tilt_y = 0.0f;
        if (tilt_enabled && dist < radius) {
            float factor = 1.0f - (dist / radius);
            target_tilt_x = (dy / radius) * max_tilt_rad * factor;
            target_tilt_y = (dx / radius) * max_tilt_rad * factor;
        }

        REQUIRE(target_tilt_x == 0.0f);
        REQUIRE(target_tilt_y == 0.0f);
    }

    SECTION("Custom max_tilt_angle scales maximum pitch and yaw proportionally") {
        int tilt_enabled = 1;
        float max_tilt_deg = 30.0f;
        const float deg2rad = 3.14159265f / 180.0f;
        const float max_tilt_rad = tilt_enabled ? (max_tilt_deg * deg2rad) : 0.0f;

        REQUIRE_THAT(max_tilt_rad, WithinAbs(30.0f * deg2rad, 0.001f));
        REQUIRE(max_tilt_rad > 0.50f); // 30 degrees ~ 0.5236 rad
    }

    SECTION("Custom drag_recession_scale and drop_zone_push are respected during drag") {
        int drag_drop_enabled = 1;
        int is_dragging = 1;
        float custom_recession = 0.65f;
        float custom_push = 80.0f;

        int drag_active = (is_dragging && drag_drop_enabled) ? 1 : 0;
        float push_factor = drag_active ? (custom_push > 0.0f ? custom_push : 50.0f) : 15.0f;
        float recession = (custom_recession >= 0.1f && custom_recession <= 1.0f) ? custom_recession : 0.80f;

        REQUIRE(drag_active == 1);
        REQUIRE_THAT(push_factor, WithinAbs(80.0f, 0.01f));
        REQUIRE_THAT(recession, WithinAbs(0.65f, 0.01f));

        // When drag_drop_enabled is toggled off
        drag_drop_enabled = 0;
        drag_active = (is_dragging && drag_drop_enabled) ? 1 : 0;
        push_factor = drag_active ? (custom_push > 0.0f ? custom_push : 50.0f) : 15.0f;
        REQUIRE(drag_active == 0);
        REQUIRE_THAT(push_factor, WithinAbs(15.0f, 0.01f));
    }
}

TEST_CASE("Custom Start Button - Element Classification & Type Enum", "[icon_hover][start_button][classification]") {
    SECTION("TE_ELEM_START_BUTTON enum has distinct non-zero value") {
        REQUIRE(TE_ELEM_START_BUTTON != TE_ELEM_UNKNOWN);
        REQUIRE(TE_ELEM_START_BUTTON != TE_ELEM_APP_ICON);
        REQUIRE(TE_ELEM_START_BUTTON != TE_ELEM_SHELL_CONTROL);
        REQUIRE(TE_ELEM_START_BUTTON != TE_ELEM_SYSTEM_TRAY);
    }

    SECTION("Icon element info correctly holds TE_ELEM_START_BUTTON") {
        TE_IconElementInfo info = {};
        info.element_type = TE_ELEM_START_BUTTON;
        SetRect(&info.buttonRect, 0, 1032, 48, 1080);
        SetRect(&info.glyphRect, 8, 1040, 40, 1072);
        wcscpy_s(info.app_id, 256, L"StartButton");

        REQUIRE(info.element_type == TE_ELEM_START_BUTTON);
        REQUIRE(info.buttonRect.right - info.buttonRect.left == 48);
        REQUIRE(info.buttonRect.bottom - info.buttonRect.top == 48);
        REQUIRE(wcscmp(info.app_id, L"StartButton") == 0);
    }

    SECTION("TE_UiaHideStartButton validates parameters") {
        REQUIRE(TE_UiaHideStartButton(NULL, TRUE) == TE_E_INVALIDARG);
        REQUIRE(TE_UiaHideStartButton(NULL, FALSE) == TE_E_INVALIDARG);
    }
}

TEST_CASE("Custom Start Button - Direct2D & WIC Image Loading (PNG & SVG)", "[icon_hover][start_button][image_load]") {
    SECTION("Argument validation on image path") {
        REQUIRE(TE_DCompLoadStartImage(NULL) == TE_E_INVALIDARG);
        REQUIRE(TE_DCompLoadStartImage(L"") == TE_E_INVALIDARG);
        REQUIRE(TE_DCompLoadStartImage(L"nonexistent_image_file_xyz_123.png") == TE_E_FAIL);
    }

    SECTION("Load PNG asset successfully") {
        HRESULT hr = TE_DCompLoadStartImage(L"Config/start_button.png");
        REQUIRE(hr == TE_S_OK);
        REQUIRE(TE_DCompIsCustomStartButtonEnabled() == 1);
    }

    SECTION("Load SVG asset successfully") {
        HRESULT hr = TE_DCompLoadStartImage(L"Config/start_button.svg");
        REQUIRE(hr == TE_S_OK);
        REQUIRE(TE_DCompIsCustomStartButtonEnabled() == 1);
    }

    SECTION("Enable and disable toggle behavior") {
        TE_DCompSetCustomStartButtonEnabled(0);
        REQUIRE(TE_DCompIsCustomStartButtonEnabled() == 0);

        TE_DCompSetCustomStartButtonEnabled(1);
        REQUIRE(TE_DCompIsCustomStartButtonEnabled() == 1);
    }
}

TEST_CASE("Custom Start Button - Bounds Retrieval & Click Hit-Testing", "[icon_hover][start_button][bounds][click]") {
    SECTION("GetStartButtonBounds validates arguments and enabled state") {
        REQUIRE(TE_DCompGetStartButtonBounds(NULL) == 0);

        TE_DCompSetCustomStartButtonEnabled(0);
        RECT r = {};
        REQUIRE(TE_DCompGetStartButtonBounds(&r) == 0);
    }

    SECTION("Click detection and hit-testing against Start Button bounds") {
        TE_IconHoverState saved_state = g_hover_state;

        g_hover_state.enabled = 1;
        g_hover_state.anim_count = 3;
        memset(&g_hover_state.icon_cache, 0, sizeof(g_hover_state.icon_cache));
        g_hover_state.icon_cache.items[0].element_type = TE_ELEM_START_BUTTON;
        g_hover_state.icon_cache.items[1].element_type = TE_ELEM_APP_ICON;
        g_hover_state.icon_cache.items[2].element_type = TE_ELEM_APP_ICON;

        g_hover_state.anim[0].center_x = 24.0f;
        g_hover_state.anim[0].center_y = 1056.0f;
        g_hover_state.anim[0].base_width = 48.0f;
        g_hover_state.anim[0].base_height = 48.0f;
        g_hover_state.anim[0].current_scale = 1.0f;
        g_hover_state.anim[0].current_pos_x = 0.0f;
        g_hover_state.anim[0].currentOffsetY = 0.0f;

        // Outside bounds: far off
        REQUIRE(TE_FrameLoopCheckStartButtonClick(200.0f, 500.0f) == 0);
        // Outside bounds: Y above button
        REQUIRE(TE_FrameLoopCheckStartButtonClick(24.0f, 900.0f) == 0);
        // Outside bounds: X to the right of button
        REQUIRE(TE_FrameLoopCheckStartButtonClick(150.0f, 1056.0f) == 0);

        // When hover state is disabled, hit check returns 0 even inside bounds
        g_hover_state.enabled = 0;
        REQUIRE(TE_FrameLoopCheckStartButtonClick(24.0f, 1056.0f) == 0);

        // When enabled, point inside bounds triggers Start button click
        g_hover_state.enabled = 1;
        REQUIRE(TE_FrameLoopCheckStartButtonClick(24.0f, 1056.0f) == 1);

        g_hover_state = saved_state;
    }
}

TEST_CASE("Custom Start Button - Magnification Falloff & Spring Physics Integration", "[icon_hover][start_button][physics]") {
    SECTION("Start button at index 0 participates in magnification and displaces neighbors") {
        const int count = 4;
        float centers[count] = { 24.0f, 72.0f, 120.0f, 168.0f };
        float scales[count] = {};
        float pos_x[count] = {};
        float pos_y[count] = {};

        float cursor_x = 24.0f; // Directly hovering over Start Button at index 0
        float radius = 100.0f;
        float max_scale = 1.5f;
        float base_w = 48.0f;
        float base_h = 48.0f;

        TE_MagnifyComputeScales(cursor_x, centers, scales, count, radius, max_scale, TE_CURVE_COSINE);

        // Start button at index 0 achieves peak scale
        REQUIRE_THAT(scales[0], WithinAbs(max_scale, 0.01f));

        // Neighboring app icon at index 1 is partially magnified with smooth falloff
        REQUIRE(scales[1] > 1.0f);
        REQUIRE(scales[1] < max_scale);

        // Distant app icons remain unmagnified
        REQUIRE_THAT(scales[2], WithinAbs(1.0f, 0.01f));
        REQUIRE_THAT(scales[3], WithinAbs(1.0f, 0.01f));

        // Compute displacement from magnification scales
        float cumulative_dx = 0.0f;
        for (int i = 0; i < count; i++) {
            float scale = scales[i];
            float extra_w = base_w * (scale - 1.0f);
            pos_x[i] = cumulative_dx - extra_w / 2.0f;
            float extra_h = base_h * (scale - 1.0f);
            pos_y[i] = -extra_h; // Grow upward into headroom
            cumulative_dx += extra_w;
        }

        // Start button grows upward into headroom
        REQUIRE(pos_y[0] < 0.0f);

        // Neighboring icons are displaced horizontally to the right
        REQUIRE(pos_x[1] > pos_x[0]);
    }

    SECTION("Spring oscillator physics on Start Button converges cleanly") {
        float current_offset_y = 0.0f;
        float target_offset_y = 0.0f;
        float velocity_offset_y = -600.0f; // Upward click/hover bounce impulse

        const float k_spring = 300.0f;
        const float c_damping = 34.64f; // 2 * sqrt(300) = critical damping
        const float dt = 0.008f; // 125 Hz

        int ticks = 0;
        while ((fabsf(current_offset_y - target_offset_y) > 0.05f || fabsf(velocity_offset_y) > 1.0f) && ticks < 300) {
            float force = -k_spring * (current_offset_y - target_offset_y) - c_damping * velocity_offset_y;
            velocity_offset_y += force * dt;
            current_offset_y += velocity_offset_y * dt;
            ticks++;
        }

        REQUIRE_THAT(current_offset_y, WithinAbs(0.0f, 0.1f));
        REQUIRE_THAT(velocity_offset_y, WithinAbs(0.0f, 5.0f));
        REQUIRE(ticks < 150); // Fully settled in under 1.2 seconds
    }
}
