/**
 * @file test_dynamic_island.cpp
 * @brief Catch2 unit test suite for Dynamic Island Base (Milestone 2 - Requirement R5).
 *
 * Covers:
 * 1. Anchoring coordinates & tray padding at 100%, 150%, 200% DPI
 * 2. DirectWrite layout bounds and character ellipsis truncation
 * 3. Hit testing for hover and click bounds
 * 4. State machine transitions (Compact <-> Expanded <-> Idle) driven by MockMediaSource
 * 5. Lifecycle and zero-overhead invariants
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwrite.h>

#include "dynamic_island.h"
#include "dynamic_island_media.h"
#include "mock_media_source.h"

#include <cmath>
#include <cstring>
#include <cwchar>

using Catch::Matchers::WithinAbs;

namespace {

template <typename T>
inline void SafeReleaseCOM(T*& ptr) {
    if (ptr != nullptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

// RAII test fixture ensuring dynamic island is cleanly initialized and reset per test
struct DynamicIslandFixture {
    DynamicIslandFixture() {
        TE_DynamicIslandConfig config = {};
        config.enabled = TRUE;
        config.padding_tray = 12;
        config.compact_width = 80;
        config.expanded_width = 240;
        config.height = 30;
        config.corner_radius = 15.0f;
        config.announce_duration_ms = 0; // 0 ms for deterministic unit test settling
        config.expand_duration_ms = 250;
        config.collapse_duration_ms = 250;
        TE_DynamicIslandInit(&config);
    }

    ~DynamicIslandFixture() {
        TE_DynamicIslandDisable();
        TE_DynamicIslandShutdown();
    }
};

} // anonymous namespace

// -----------------------------------------------------------------------------
// 1. Anchoring coordinates & tray padding at 100%, 150%, 200% DPI
// -----------------------------------------------------------------------------

TEST_CASE("Dynamic Island - Anchoring Coordinates & Tray Padding Across DPI Scales", "[dynamic_island][anchoring][dpi]") {
    DynamicIslandFixture fixture;

    SECTION("100% DPI (96 DPI) - Baseline Geometry & Vertical Centering") {
        TE_DynamicIslandEnable(NULL, 96);

        RECT taskbar = { 0, 1040, 1920, 1080 }; // 40px height
        RECT mock_tray = { 1700, 1040, 1920, 1080 };

        // Create temporary mock tray window to test HWND rect discovery
        HWND tray_wnd = CreateWindowExA(
            WS_EX_TOOLWINDOW, "STATIC", "MockTray96",
            WS_POPUP | WS_VISIBLE,
            mock_tray.left, mock_tray.top,
            mock_tray.right - mock_tray.left,
            mock_tray.bottom - mock_tray.top,
            NULL, NULL, NULL, NULL
        );

        TE_DynamicIslandOnGeometryChanged(&taskbar, tray_wnd);

        RECT bounds = {};
        REQUIRE(TE_DynamicIslandGetBounds(&bounds) == TRUE);

        // At 100% DPI: padding = 12px, compact_w = 80px, height = 30px
        // pill_right = 1700 - 12 = 1688
        // pill_left  = 1688 - 80 = 1608
        // pill_top   = 1040 + (40 - 30)/2 = 1045
        // pill_bottom = 1045 + 30 = 1075
        REQUIRE(bounds.right == 1688);
        REQUIRE(bounds.left == 1608);
        REQUIRE(bounds.top == 1045);
        REQUIRE(bounds.bottom == 1075);

        // Verify taskbar baseline centering: (top + bottom)/2 == taskbar center (1060)
        REQUIRE((bounds.top + bounds.bottom) / 2 == (taskbar.top + taskbar.bottom) / 2);

        float vx = 0.0f, vy = 0.0f;
        TE_DynamicIslandGetVisualOffset(&vx, &vy);
        REQUIRE_THAT(vx, WithinAbs(1608.0f, 0.01f));
        REQUIRE_THAT(vy, WithinAbs(125.0f, 0.01f)); // 120 (headroom) + 5

        if (tray_wnd) DestroyWindow(tray_wnd);
    }

    SECTION("150% DPI (144 DPI) - Scaled Padding & Dimensions") {
        TE_DynamicIslandEnable(NULL, 144);
        TE_DynamicIslandOnDpiChanged(144);

        RECT taskbar = { 0, 1020, 1920, 1080 }; // 60px height
        RECT mock_tray = { 1650, 1020, 1920, 1080 };

        HWND tray_wnd = CreateWindowExA(
            WS_EX_TOOLWINDOW, "STATIC", "MockTray144",
            WS_POPUP | WS_VISIBLE,
            mock_tray.left, mock_tray.top,
            mock_tray.right - mock_tray.left,
            mock_tray.bottom - mock_tray.top,
            NULL, NULL, NULL, NULL
        );

        TE_DynamicIslandOnGeometryChanged(&taskbar, tray_wnd);

        RECT bounds = {};
        REQUIRE(TE_DynamicIslandGetBounds(&bounds) == TRUE);

        // At 150% DPI (scale 1.5): padding = 18px, compact_w = 120px, height = 45px
        // pill_right = 1650 - 18 = 1632
        // pill_left  = 1632 - 120 = 1512
        // pill_top   = 1020 + (60 - 45)/2 = 1027.5 -> floor = 1027
        // pill_bottom = 1027.5 + 45 = 1072.5 -> ceil = 1073
        REQUIRE(bounds.right == 1632);
        REQUIRE(bounds.left == 1512);
        REQUIRE(bounds.top == 1027);
        REQUIRE(bounds.bottom == 1073);

        float vx = 0.0f, vy = 0.0f;
        TE_DynamicIslandGetVisualOffset(&vx, &vy);
        REQUIRE_THAT(vx, WithinAbs(1512.0f, 0.01f));
        REQUIRE_THAT(vy, WithinAbs(127.5f, 0.01f)); // 120 + 7.5

        if (tray_wnd) DestroyWindow(tray_wnd);
    }

    SECTION("200% DPI (192 DPI) - 4K High-DPI Scaling") {
        TE_DynamicIslandEnable(NULL, 192);
        TE_DynamicIslandOnDpiChanged(192);

        RECT taskbar = { 0, 1000, 1920, 1080 }; // 80px height
        RECT mock_tray = { 1600, 1000, 1920, 1080 };

        HWND tray_wnd = CreateWindowExA(
            WS_EX_TOOLWINDOW, "STATIC", "MockTray192",
            WS_POPUP | WS_VISIBLE,
            mock_tray.left, mock_tray.top,
            mock_tray.right - mock_tray.left,
            mock_tray.bottom - mock_tray.top,
            NULL, NULL, NULL, NULL
        );

        TE_DynamicIslandOnGeometryChanged(&taskbar, tray_wnd);

        RECT bounds = {};
        REQUIRE(TE_DynamicIslandGetBounds(&bounds) == TRUE);

        // At 200% DPI (scale 2.0): padding = 24px, compact_w = 160px, height = 60px
        // pill_right = 1600 - 24 = 1576
        // pill_left  = 1576 - 160 = 1416
        // pill_top   = 1000 + (80 - 60)/2 = 1010
        // pill_bottom = 1010 + 60 = 1070
        REQUIRE(bounds.right == 1576);
        REQUIRE(bounds.left == 1416);
        REQUIRE(bounds.top == 1010);
        REQUIRE(bounds.bottom == 1070);

        // Verify taskbar baseline centering
        REQUIRE((bounds.top + bounds.bottom) / 2 == (taskbar.top + taskbar.bottom) / 2);

        float vx = 0.0f, vy = 0.0f;
        TE_DynamicIslandGetVisualOffset(&vx, &vy);
        REQUIRE_THAT(vx, WithinAbs(1416.0f, 0.01f));
        REQUIRE_THAT(vy, WithinAbs(130.0f, 0.01f)); // 120 + 10

        if (tray_wnd) DestroyWindow(tray_wnd);
    }

    SECTION("Fallback Anchoring When TrayNotifyWnd is NULL (Headless/CI)") {
        TE_DynamicIslandEnable(NULL, 96);

        RECT taskbar = { 0, 1040, 1920, 1080 };
        TE_DynamicIslandOnGeometryChanged(&taskbar, NULL);

        RECT bounds = {};
        REQUIRE(TE_DynamicIslandGetBounds(&bounds) == TRUE);

        // Fallback tray width = 160px at 100% DPI
        // tray_left = 1920
        // pill_right = 1920 - 12 = 1908
        // pill_left  = 1908 - 80 = 1828
        REQUIRE(bounds.right == 1908);
        REQUIRE(bounds.left == 1828);
        REQUIRE(bounds.top == 1045);
        REQUIRE(bounds.bottom == 1075);
    }

    SECTION("Configurable Tray Padding Dynamic Recomputation") {
        TE_DynamicIslandEnable(NULL, 96);
        RECT taskbar = { 0, 1040, 1920, 1080 };
        TE_DynamicIslandOnGeometryChanged(&taskbar, NULL);

        TE_DynamicIslandConfig custom_cfg = {};
        custom_cfg.enabled = TRUE;
        custom_cfg.padding_tray = 24; // Doubled margin
        custom_cfg.compact_width = 80;
        custom_cfg.expanded_width = 240;
        custom_cfg.height = 30;
        custom_cfg.corner_radius = 15.0f;
        TE_DynamicIslandUpdateConfig(&custom_cfg);

        RECT bounds = {};
        REQUIRE(TE_DynamicIslandGetBounds(&bounds) == TRUE);
        // pill_right = 1920 - 24 = 1896
        // pill_left  = 1896 - 80 = 1816
        REQUIRE(bounds.right == 1896);
        REQUIRE(bounds.left == 1816);
    }
}

// -----------------------------------------------------------------------------
// 2. DirectWrite layout bounds and character ellipsis truncation
// -----------------------------------------------------------------------------

TEST_CASE("Dynamic Island - DirectWrite Typography Layout Bounds & Character Ellipsis", "[dynamic_island][directwrite][typography]") {
    IDWriteFactory* dwrite_factory = nullptr;
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&dwrite_factory)
    );
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(dwrite_factory != nullptr);

    IDWriteTextFormat* text_format = nullptr;
    hr = dwrite_factory->CreateTextFormat(
        L"Segoe UI",
        NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.5f,
        L"en-us",
        &text_format
    );
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(text_format != nullptr);

    text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    IDWriteInlineObject* ellipsis_sign = nullptr;
    hr = dwrite_factory->CreateEllipsisTrimmingSign(text_format, &ellipsis_sign);
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(ellipsis_sign != nullptr);

    DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    hr = text_format->SetTrimming(&trimming, ellipsis_sign);
    REQUIRE(SUCCEEDED(hr));

    SECTION("Standard Short String Fits Within Bounds Without Wrapping") {
        const wchar_t* short_title = L"Track 01";
        IDWriteTextLayout* layout = nullptr;
        hr = dwrite_factory->CreateTextLayout(
            short_title,
            (UINT32)wcslen(short_title),
            text_format,
            120.0f,
            20.0f,
            &layout
        );
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(layout != nullptr);

        DWRITE_TEXT_METRICS metrics = {};
        hr = layout->GetMetrics(&metrics);
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(metrics.width <= 120.0f);
        REQUIRE(metrics.lineCount == 1);

        SafeReleaseCOM(layout);
    }

    SECTION("Extremely Long Title Enforces Bounds & Single-Line No-Wrap") {
        const wchar_t* long_title =
            L"This Is An Extremely Long Audio Track Title That Vastly Exceeds The Maximum "
            L"Allowed Visual Width Of The Dynamic Island Pill Container And Must Be Ellipsis Trimmed";

        IDWriteTextLayout* layout = nullptr;
        hr = dwrite_factory->CreateTextLayout(
            long_title,
            (UINT32)wcslen(long_title),
            text_format,
            100.0f, // Constrained width in pill
            20.0f,
            &layout
        );
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(layout != nullptr);

        hr = layout->SetTrimming(&trimming, ellipsis_sign);
        REQUIRE(SUCCEEDED(hr));

        DWRITE_TEXT_METRICS metrics = {};
        hr = layout->GetMetrics(&metrics);
        REQUIRE(SUCCEEDED(hr));

        // DirectWrite enforces layout box bounding limit
        REQUIRE(metrics.width <= 101.0f);
        REQUIRE(metrics.lineCount == 1);

        // Verify trimming configuration retrieval
        DWRITE_TRIMMING retrieved_trimming = {};
        IDWriteInlineObject* retrieved_sign = nullptr;
        hr = layout->GetTrimming(&retrieved_trimming, &retrieved_sign);
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(retrieved_trimming.granularity == DWRITE_TRIMMING_GRANULARITY_CHARACTER);
        REQUIRE(retrieved_sign != nullptr);

        SafeReleaseCOM(retrieved_sign);
        SafeReleaseCOM(layout);
    }

    SECTION("Boundary Condition - Single Character & Empty String Handling") {
        const wchar_t* single_char = L"A";
        IDWriteTextLayout* layout_single = nullptr;
        hr = dwrite_factory->CreateTextLayout(
            single_char, 1, text_format, 100.0f, 20.0f, &layout_single
        );
        REQUIRE(SUCCEEDED(hr));
        DWRITE_TEXT_METRICS metrics_single = {};
        REQUIRE(SUCCEEDED(layout_single->GetMetrics(&metrics_single)));
        REQUIRE(metrics_single.width > 0.0f);
        REQUIRE(metrics_single.width <= 100.0f);
        REQUIRE(metrics_single.lineCount == 1);
        SafeReleaseCOM(layout_single);

        const wchar_t* empty_str = L"";
        IDWriteTextLayout* layout_empty = nullptr;
        hr = dwrite_factory->CreateTextLayout(
            empty_str, 0, text_format, 100.0f, 20.0f, &layout_empty
        );
        REQUIRE(SUCCEEDED(hr));
        DWRITE_TEXT_METRICS metrics_empty = {};
        REQUIRE(SUCCEEDED(layout_empty->GetMetrics(&metrics_empty)));
        REQUIRE(metrics_empty.width == 0.0f);
        SafeReleaseCOM(layout_empty);
    }

    SafeReleaseCOM(ellipsis_sign);
    SafeReleaseCOM(text_format);
    SafeReleaseCOM(dwrite_factory);
}

// -----------------------------------------------------------------------------
// 3. Hit testing for hover and click bounds
// -----------------------------------------------------------------------------

TEST_CASE("Dynamic Island - Hit Testing for Hover and Click Bounds", "[dynamic_island][hittest][interaction]") {
    DynamicIslandFixture fixture;
    TE_DynamicIslandEnable(NULL, 96);

    RECT taskbar = { 0, 1040, 1920, 1080 };
    RECT mock_tray = { 1700, 1040, 1920, 1080 };

    HWND tray_wnd = CreateWindowExA(
        WS_EX_TOOLWINDOW, "STATIC", "MockTrayHit",
        WS_POPUP,
        mock_tray.left, mock_tray.top,
        mock_tray.right - mock_tray.left,
        mock_tray.bottom - mock_tray.top,
        NULL, NULL, NULL, NULL
    );

    TE_DynamicIslandOnGeometryChanged(&taskbar, tray_wnd);

    RECT bounds = {};
    REQUIRE(TE_DynamicIslandGetBounds(&bounds) == TRUE);
    // Bounds: left=1608, top=1045, right=1688, bottom=1075

    SECTION("Interior and Boundary Coordinate Hit-Testing") {
        float center_x = (float)(bounds.left + bounds.right) / 2.0f;
        float center_y = (float)(bounds.top + bounds.bottom) / 2.0f;

        POINT pt_inside = { (LONG)center_x, (LONG)center_y };
        REQUIRE(PtInRect(&bounds, pt_inside) != FALSE);

        POINT pt_top_left = { bounds.left, bounds.top };
        REQUIRE(PtInRect(&bounds, pt_top_left) != FALSE);

        POINT pt_just_inside_br = { bounds.right - 1, bounds.bottom - 1 };
        REQUIRE(PtInRect(&bounds, pt_just_inside_br) != FALSE);

        // Outside coordinates
        POINT pt_outside_left = { bounds.left - 1, (LONG)center_y };
        REQUIRE(PtInRect(&bounds, pt_outside_left) == FALSE);

        POINT pt_outside_right = { bounds.right + 1, (LONG)center_y };
        REQUIRE(PtInRect(&bounds, pt_outside_right) == FALSE);

        POINT pt_outside_top = { (LONG)center_x, bounds.top - 1 };
        REQUIRE(PtInRect(&bounds, pt_outside_top) == FALSE);

        POINT pt_outside_bottom = { (LONG)center_x, bounds.bottom + 1 };
        REQUIRE(PtInRect(&bounds, pt_outside_bottom) == FALSE);

        POINT pt_distant = { 500, 500 };
        REQUIRE(PtInRect(&bounds, pt_distant) == FALSE);
    }

    SECTION("Hover State Expansion and Collapse Convergence") {
        MockMediaSource mock_source;
        mock_source.Initialize();
        mock_source.SetMockTrack(L"Song", L"Artist", TEMediaPlaybackStatus::Playing);
        TE_DynamicIslandSetMediaSource(&mock_source);

        // Step 1: Settling in compact state (80px)
        for (int i = 0; i < 40; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }
        REQUIRE_THAT(TE_DynamicIslandGetCurrentWidth(), WithinAbs(80.0f, 0.5f));
        REQUIRE(TE_DynamicIslandIsVisible() == TRUE);

        // Step 2: Hover over island center
        float center_x = (float)(bounds.left + bounds.right) / 2.0f;
        float center_y = (float)(bounds.top + bounds.bottom) / 2.0f;
        TE_DynamicIslandOnMouseMove(center_x, center_y);

        // Step 3: Frame update loop expands towards 240px
        for (int i = 0; i < 40; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }
        REQUIRE_THAT(TE_DynamicIslandGetCurrentWidth(), WithinAbs(240.0f, 1.0f));

        // Step 4: Mouse leave collapses back towards 80px
        TE_DynamicIslandOnMouseLeave();
        for (int i = 0; i < 40; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }
        REQUIRE_THAT(TE_DynamicIslandGetCurrentWidth(), WithinAbs(80.0f, 1.0f));
    }

    SECTION("Click Action Gating - Rejected When Inactive / Accepted When Active") {
        MockMediaSource mock_source;
        mock_source.Initialize();
        mock_source.SetMockTrack(L"Song", L"Artist", TEMediaPlaybackStatus::Inactive);
        TE_DynamicIslandSetMediaSource(&mock_source);

        // Frame updates with inactive media -> opacity converges to 0
        for (int i = 0; i < 30; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }
        REQUIRE(TE_DynamicIslandIsVisible() == FALSE);

        float center_x = (float)(bounds.left + bounds.right) / 2.0f;
        float center_y = (float)(bounds.top + bounds.bottom) / 2.0f;

        // Click while hidden must return FALSE and NOT toggle media
        REQUIRE(TE_DynamicIslandCheckClick(center_x, center_y) == FALSE);

        // Now activate media
        mock_source.SetMockTrack(L"Song", L"Artist", TEMediaPlaybackStatus::Playing);
        for (int i = 0; i < 30; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }
        REQUIRE(TE_DynamicIslandIsVisible() == TRUE);

        // Click outside bounds returns FALSE
        REQUIRE(TE_DynamicIslandCheckClick(500.0f, 500.0f) == FALSE);

        // Click inside bounds returns TRUE and toggles playback to Paused
        REQUIRE(TE_DynamicIslandCheckClick(center_x, center_y) == TRUE);

        TEMediaStateSnapshot snap = {};
        REQUIRE(mock_source.GetLatestSnapshot(&snap) == true);
        REQUIRE(snap.status == TEMediaPlaybackStatus::Paused);
    }

    if (tray_wnd) DestroyWindow(tray_wnd);
}

// -----------------------------------------------------------------------------
// 4. State machine transitions (Compact <-> Expanded <-> Idle) driven by MockMediaSource
// -----------------------------------------------------------------------------

TEST_CASE("Dynamic Island - State Machine Transitions Driven by MockMediaSource", "[dynamic_island][statemachine][media]") {
    DynamicIslandFixture fixture;
    MockMediaSource mock_source;
    mock_source.Initialize();
    TE_DynamicIslandSetMediaSource(&mock_source);
    TE_DynamicIslandEnable(NULL, 96);

    SECTION("Idle State - Inactive / Stopped Playback Produces 0 Opacity") {
        mock_source.SimulateStatusChange(TEMediaPlaybackStatus::Inactive);

        for (int i = 0; i < 30; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }

        REQUIRE(TE_DynamicIslandIsVisible() == FALSE);
        REQUIRE_THAT(TE_DynamicIslandGetCurrentOpacity(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("Transition: Idle -> Playing -> Compact Settling") {
        mock_source.SetMockTrack(L"Bohemian Rhapsody", L"Queen", TEMediaPlaybackStatus::Playing);

        // Frame ticks converge opacity to 1.0f and compact width to 80px
        for (int i = 0; i < 40; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }

        REQUIRE(TE_DynamicIslandIsVisible() == TRUE);
        REQUIRE_THAT(TE_DynamicIslandGetCurrentOpacity(), WithinAbs(1.0f, 0.01f));
        REQUIRE_THAT(TE_DynamicIslandGetCurrentWidth(), WithinAbs(80.0f, 0.5f));
    }

    SECTION("Transition: Playing -> Paused Keeps Visual Visible") {
        mock_source.SetMockTrack(L"Hotel California", L"Eagles", TEMediaPlaybackStatus::Playing);
        for (int i = 0; i < 30; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }
        REQUIRE(TE_DynamicIslandIsVisible() == TRUE);

        mock_source.SimulateStatusChange(TEMediaPlaybackStatus::Paused);
        for (int i = 0; i < 30; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }

        // Paused media remains visible in compact mode
        REQUIRE(TE_DynamicIslandIsVisible() == TRUE);
        REQUIRE_THAT(TE_DynamicIslandGetCurrentOpacity(), WithinAbs(1.0f, 0.01f));
    }

    SECTION("Transition: Playing -> Stopped Fades Out to Hidden") {
        mock_source.SetMockTrack(L"Track", L"Artist", TEMediaPlaybackStatus::Playing);
        for (int i = 0; i < 30; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }
        REQUIRE(TE_DynamicIslandIsVisible() == TRUE);

        mock_source.SimulateStatusChange(TEMediaPlaybackStatus::Stopped);
        for (int i = 0; i < 40; i++) {
            TE_DynamicIslandUpdateFrame(0.016f);
        }

        REQUIRE_THAT(TE_DynamicIslandGetCurrentOpacity(), WithinAbs(0.0f, 0.01f));
        REQUIRE(TE_DynamicIslandIsVisible() == FALSE);
    }
}

// -----------------------------------------------------------------------------
// 5. Lifecycle and subsystem invariants
// -----------------------------------------------------------------------------

TEST_CASE("Dynamic Island - Lifecycle and Subsystem Invariants", "[dynamic_island][lifecycle]") {
    SECTION("Disabled Subsystem Has Zero Presence") {
        TE_DynamicIslandConfig cfg = {};
        cfg.enabled = FALSE;
        TE_DynamicIslandInit(&cfg);

        REQUIRE(TE_DynamicIslandIsEnabled() == FALSE);
        REQUIRE(TE_DynamicIslandIsVisible() == FALSE);
        REQUIRE_THAT(TE_DynamicIslandGetCurrentOpacity(), WithinAbs(0.0f, 0.001f));

        // Frame update does nothing when disabled
        TE_DynamicIslandUpdateFrame(0.016f);
        REQUIRE(TE_DynamicIslandIsVisible() == FALSE);
    }

    SECTION("Clean Enable -> Disable -> Enable Cycle") {
        TE_DynamicIslandConfig cfg = {};
        cfg.enabled = TRUE;
        TE_DynamicIslandInit(&cfg);
        REQUIRE(TE_DynamicIslandEnable(NULL, 96) == TRUE);
        REQUIRE(TE_DynamicIslandIsEnabled() == TRUE);

        TE_DynamicIslandDisable();
        REQUIRE(TE_DynamicIslandIsEnabled() == FALSE);
        REQUIRE(TE_DynamicIslandIsVisible() == FALSE);

        REQUIRE(TE_DynamicIslandEnable(NULL, 96) == TRUE);
        REQUIRE(TE_DynamicIslandIsEnabled() == TRUE);

        TE_DynamicIslandShutdown();
        REQUIRE(TE_DynamicIslandIsEnabled() == FALSE);
    }

    SECTION("Visual Tree Integration Null-Safety") {
        // Must reject NULL handles gracefully
        REQUIRE(TE_DynamicIslandAttachVisualTree(nullptr, nullptr, nullptr) == FALSE);
        TE_DynamicIslandDetachVisualTree(); // Must not crash when detached without attach
    }
}

TEST_CASE("Dynamic Island - IsDirty Drops to False on Sequential Identical Ticks", "[dynamic_island][dirty]") {
    DynamicIslandFixture fixture;
    TE_DynamicIslandEnable(NULL, 96);
    
    TE_DynamicIslandOnDpiChanged(144);
    TE_DynamicIslandUpdateFrame(0.016f); 
    
    REQUIRE(TE_DynamicIslandIsDirty() == TRUE);
    REQUIRE(TE_DynamicIslandIsDirty() == FALSE);
    
    TE_DynamicIslandUpdateFrame(0.016f);
    REQUIRE(TE_DynamicIslandIsDirty() == FALSE);
    
    TE_DynamicIslandOnDpiChanged(96);
    TE_DynamicIslandUpdateFrame(0.016f);
    REQUIRE(TE_DynamicIslandIsDirty() == TRUE);
    REQUIRE(TE_DynamicIslandIsDirty() == FALSE);
}
