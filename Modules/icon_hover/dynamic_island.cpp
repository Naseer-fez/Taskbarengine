#include "dynamic_island.h"
#include "dynamic_island_media.h"
#include "gsmtc_source.h"
#include "dcomp_overlay.h"

#include <dcomp.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dxgi.h>

#include <cmath>
#include <algorithm>
#include <memory>
#include <cstring>
#include <cwchar>

template <typename T>
static inline void SafeRelease(T*& ptr) {
    if (ptr != nullptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

// Module State
static TE_DynamicIslandConfig s_config = {
    FALSE,  // enabled
    12,     // padding_tray
    80,     // compact_width
    240,    // expanded_width
    30,     // height
    15.0f,  // corner_radius
    3000,   // announce_duration_ms
    250,    // expand_duration_ms
    250     // collapse_duration_ms
};

static BOOL s_initialized = FALSE;
static BOOL s_enabled = FALSE;
static bool s_is_hovered = false;
static uint32_t s_dpi = 96;
static float s_headroom_y = 120.0f;

static HWND s_primary_taskbar = NULL;
static HWND s_tray_hwnd = NULL;
static RECT s_taskbar_rect = { 0, 1040, 1920, 1080 };
static RECT s_hit_rect = { 0, 0, 0, 0 };
static float s_visual_x = 0.0f;
static float s_visual_y = 0.0f;

// Sizing & Animation
static float s_current_width = 80.0f;
static float s_target_width = 80.0f;
static float s_current_opacity = 0.0f;
static float s_target_opacity = 0.0f;
static float s_rendered_width = 0.0f;
static float s_rendered_opacity = -1.0f;
static bool s_state_dirty = true;

static uint64_t s_announce_end_time_ms = 0;
static uint64_t s_last_track_change_id = 0;

// DirectComposition & Direct2D / DirectWrite handles
static IDCompositionVisual* s_island_visual = nullptr;
static IDCompositionTranslateTransform* s_translate_transform = nullptr;
static IDCompositionEffectGroup* s_island_effect = nullptr;
static IDCompositionSurface* s_surface = nullptr;
static UINT s_surface_width = 0;
static UINT s_surface_height = 0;

static IDCompositionDevice* s_dcomp_device = nullptr;
static IDCompositionVisual* s_root_visual = nullptr;
static ID2D1Factory* s_d2d_factory = nullptr;

static IDWriteFactory* s_dwrite_factory = nullptr;
static IDWriteTextFormat* s_text_format_title = nullptr;
static IDWriteTextFormat* s_text_format_artist = nullptr;
static IDWriteInlineObject* s_ellipsis_title = nullptr;
static IDWriteInlineObject* s_ellipsis_artist = nullptr;

// Media Source
static IMediaSource* s_media_source = nullptr;
static std::unique_ptr<IMediaSource> s_owned_media_source;
static TEMediaStateSnapshot s_current_media_state = {};
static TE_DynamicIslandWakeCallback s_wake_callback = nullptr;

// Helper Declarations
static void UpdateLayoutAndBounds();
static void EnsureSurface();
static void EnsureDirectWrite();
static void RenderIslandSurface();
static BOOL InternalAttachVisuals();

static void UpdateLayoutAndBounds() {
    float dpi_scale = (s_dpi > 0) ? ((float)s_dpi / 96.0f) : 1.0f;
    float pill_h = (float)s_config.height * dpi_scale;
    float padding_px = (float)s_config.padding_tray * dpi_scale;
    float current_w = (s_current_width > 0.0f) ? s_current_width : ((float)s_config.compact_width * dpi_scale);

    RECT tb = s_taskbar_rect;
    if (s_primary_taskbar && IsWindow(s_primary_taskbar)) {
        GetWindowRect(s_primary_taskbar, &tb);
        s_taskbar_rect = tb;
    } else if (tb.right <= tb.left || tb.bottom <= tb.top) {
        tb.left = 0;
        tb.top = 1040;
        tb.right = 1920;
        tb.bottom = 1080;
        s_taskbar_rect = tb;
    }

    RECT tray_rect = {};
    if (s_tray_hwnd && IsWindow(s_tray_hwnd)) {
        if (!IsWindowVisible(s_tray_hwnd)) {
            tray_rect.right = 0; tray_rect.left = 0;
        } else {
            GetWindowRect(s_tray_hwnd, &tray_rect);
        }
    } else if (s_primary_taskbar && IsWindow(s_primary_taskbar)) {
        s_tray_hwnd = FindWindowExW(s_primary_taskbar, NULL, L"TrayNotifyWnd", NULL);
        if (s_tray_hwnd && IsWindow(s_tray_hwnd) && IsWindowVisible(s_tray_hwnd)) {
            GetWindowRect(s_tray_hwnd, &tray_rect);
        }
    }

    if (tray_rect.right <= tray_rect.left) {
        /* Safe fallback to taskbar right edge if TrayNotifyWnd cannot be found (e.g. headless/CI) */
        tray_rect.right = tb.right;
        tray_rect.left = tb.right;
        tray_rect.top = tb.top;
        tray_rect.bottom = tb.bottom;
    }

    float pill_screen_right = (float)tray_rect.left - padding_px;
    float pill_screen_left = pill_screen_right - current_w;
    float tb_height = (float)(tb.bottom - tb.top);
    float pill_screen_top = (float)tb.top + (tb_height - pill_h) / 2.0f;
    float pill_screen_bottom = pill_screen_top + pill_h;

    s_hit_rect.left = (LONG)std::floor(pill_screen_left);
    s_hit_rect.top = (LONG)std::floor(pill_screen_top);
    s_hit_rect.right = (LONG)std::ceil(pill_screen_right);
    s_hit_rect.bottom = (LONG)std::ceil(pill_screen_bottom);

    s_visual_x = pill_screen_left - (float)tb.left;
    s_visual_y = s_headroom_y + (tb_height - pill_h) / 2.0f;

    if (s_translate_transform) {
        s_translate_transform->SetOffsetX(s_visual_x);
        s_translate_transform->SetOffsetY(s_visual_y);
    }
}

static void EnsureSurface() {
    if (!s_dcomp_device || !s_island_visual) return;
    float dpi_scale = (s_dpi > 0) ? ((float)s_dpi / 96.0f) : 1.0f;
    UINT needed_w = (UINT)std::max<float>(512.0f, (float)s_config.expanded_width * dpi_scale + 64.0f);
    UINT needed_h = (UINT)std::max<float>(64.0f, (float)s_config.height * dpi_scale + 32.0f);

    if (s_surface && s_surface_width >= needed_w && s_surface_height >= needed_h) {
        return;
    }

    SafeRelease(s_surface);
    s_surface_width = needed_w;
    s_surface_height = needed_h;

    HRESULT hr = s_dcomp_device->CreateSurface(
        s_surface_width,
        s_surface_height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_ALPHA_MODE_PREMULTIPLIED,
        &s_surface
    );

    if (SUCCEEDED(hr) && s_surface) {
        s_island_visual->SetContent(s_surface);
        s_state_dirty = true;
    }
}

static void EnsureDirectWrite() {
    if (!s_dwrite_factory) {
        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&s_dwrite_factory)
        );
        if (FAILED(hr) || !s_dwrite_factory) return;
    }

    float dpi_scale = (s_dpi > 0) ? ((float)s_dpi / 96.0f) : 1.0f;

    SafeRelease(s_ellipsis_title);
    SafeRelease(s_ellipsis_artist);
    SafeRelease(s_text_format_title);
    SafeRelease(s_text_format_artist);

    float title_size = 10.5f * dpi_scale;
    float artist_size = 9.0f * dpi_scale;

    const wchar_t* font_family = L"Segoe UI Variable Text";
    HRESULT hr = s_dwrite_factory->CreateTextFormat(
        font_family,
        NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        title_size,
        L"en-us",
        &s_text_format_title
    );
    if (FAILED(hr) || !s_text_format_title) {
        font_family = L"Segoe UI";
        hr = s_dwrite_factory->CreateTextFormat(
            font_family,
            NULL,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            title_size,
            L"en-us",
            &s_text_format_title
        );
    }

    if (SUCCEEDED(hr) && s_text_format_title) {
        s_text_format_title->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        s_text_format_title->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        s_text_format_title->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        hr = s_dwrite_factory->CreateEllipsisTrimmingSign(s_text_format_title, &s_ellipsis_title);
        if (SUCCEEDED(hr) && s_ellipsis_title) {
            s_text_format_title->SetTrimming(&trimming, s_ellipsis_title);
        }
    }

    hr = s_dwrite_factory->CreateTextFormat(
        font_family,
        NULL,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        artist_size,
        L"en-us",
        &s_text_format_artist
    );
    if (FAILED(hr) || !s_text_format_artist) {
        font_family = L"Segoe UI";
        hr = s_dwrite_factory->CreateTextFormat(
            font_family,
            NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            artist_size,
            L"en-us",
            &s_text_format_artist
        );
    }

    if (SUCCEEDED(hr) && s_text_format_artist) {
        s_text_format_artist->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        s_text_format_artist->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        s_text_format_artist->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        hr = s_dwrite_factory->CreateEllipsisTrimmingSign(s_text_format_artist, &s_ellipsis_artist);
        if (SUCCEEDED(hr) && s_ellipsis_artist) {
            s_text_format_artist->SetTrimming(&trimming, s_ellipsis_artist);
        }
    }
}

static void DrawMusicNote(ID2D1RenderTarget* rt, ID2D1Brush* brush, float x, float y, float size) {
    if (!rt || !brush) return;

    // Single clean musical eighth note
    D2D1_ELLIPSE note_head = D2D1::Ellipse(
        D2D1::Point2F(x + size * 0.35f, y + size * 0.72f),
        size * 0.24f,
        size * 0.17f
    );
    rt->FillEllipse(&note_head, brush);

    D2D1_RECT_F stem_rect = D2D1::RectF(
        x + size * 0.48f,
        y + size * 0.18f,
        x + size * 0.58f,
        y + size * 0.72f
    );
    rt->FillRectangle(&stem_rect, brush);

    // Note flag using path geometry
    if (s_d2d_factory) {
        ID2D1PathGeometry* flag_geo = nullptr;
        HRESULT hr = s_d2d_factory->CreatePathGeometry(&flag_geo);
        if (SUCCEEDED(hr) && flag_geo) {
            ID2D1GeometrySink* sink = nullptr;
            hr = flag_geo->Open(&sink);
            if (SUCCEEDED(hr) && sink) {
                sink->BeginFigure(D2D1::Point2F(x + size * 0.58f, y + size * 0.18f), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddBezier(D2D1::BezierSegment(
                    D2D1::Point2F(x + size * 0.85f, y + size * 0.28f),
                    D2D1::Point2F(x + size * 0.82f, y + size * 0.48f),
                    D2D1::Point2F(x + size * 0.58f, y + size * 0.55f)
                ));
                sink->AddLine(D2D1::Point2F(x + size * 0.58f, y + size * 0.42f));
                sink->AddBezier(D2D1::BezierSegment(
                    D2D1::Point2F(x + size * 0.72f, y + size * 0.38f),
                    D2D1::Point2F(x + size * 0.72f, y + size * 0.28f),
                    D2D1::Point2F(x + size * 0.58f, y + size * 0.25f)
                ));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                sink->Release();
                rt->FillGeometry(flag_geo, brush);
            }
            flag_geo->Release();
        }
    }
}

static void DrawPlayIcon(ID2D1RenderTarget* rt, ID2D1Brush* brush, float x, float y, float size) {
    if (!rt || !brush || !s_d2d_factory) return;

    ID2D1PathGeometry* triangle_geo = nullptr;
    HRESULT hr = s_d2d_factory->CreatePathGeometry(&triangle_geo);
    if (SUCCEEDED(hr) && triangle_geo) {
        ID2D1GeometrySink* sink = nullptr;
        hr = triangle_geo->Open(&sink);
        if (SUCCEEDED(hr) && sink) {
            sink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(x, y + size));
            sink->AddLine(D2D1::Point2F(x + size * 0.90f, y + size * 0.50f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            rt->FillGeometry(triangle_geo, brush);
        }
        triangle_geo->Release();
    }
}

static void DrawPauseIcon(ID2D1RenderTarget* rt, ID2D1Brush* brush, float x, float y, float size) {
    if (!rt || !brush) return;

    float bar_w = size * 0.28f;
    float gap = size * 0.30f;
    D2D1_ROUNDED_RECT bar1 = D2D1::RoundedRect(D2D1::RectF(x, y, x + bar_w, y + size), 1.0f, 1.0f);
    D2D1_ROUNDED_RECT bar2 = D2D1::RoundedRect(D2D1::RectF(x + bar_w + gap, y, x + 2.0f * bar_w + gap, y + size), 1.0f, 1.0f);

    rt->FillRoundedRectangle(&bar1, brush);
    rt->FillRoundedRectangle(&bar2, brush);
}

static void RenderIslandSurface() {
    if (!s_surface || !s_d2d_factory) return;

    IDXGISurface* dxgi_surface = nullptr;
    POINT offset_point = {};
    HRESULT hr = s_surface->BeginDraw(NULL, __uuidof(IDXGISurface), reinterpret_cast<void**>(&dxgi_surface), &offset_point);
    if (FAILED(hr) || !dxgi_surface) return;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    ID2D1RenderTarget* rt = nullptr;
    hr = s_d2d_factory->CreateDxgiSurfaceRenderTarget(dxgi_surface, &props, &rt);
    if (SUCCEEDED(hr) && rt) {
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        float dpi_scale = (s_dpi > 0) ? ((float)s_dpi / 96.0f) : 1.0f;
        float pill_w = s_current_width;
        float pill_h = (float)s_config.height * dpi_scale;
        float corner_radius = pill_h / 2.0f;

        float ox = (float)offset_point.x;
        float oy = (float)offset_point.y;

        ID2D1SolidColorBrush* bg_brush = nullptr;
        ID2D1SolidColorBrush* border_brush = nullptr;
        ID2D1SolidColorBrush* white_brush = nullptr;
        ID2D1SolidColorBrush* muted_brush = nullptr;

        rt->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.75f), &bg_brush);
        rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f), &border_brush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.95f, 0.95f), &white_brush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.70f, 0.70f, 0.74f, 0.85f), &muted_brush);

        // 1. Frosted acrylic rounded background container & subtle border
        D2D1_ROUNDED_RECT pill_rect = D2D1::RoundedRect(
            D2D1::RectF(ox + 0.5f, oy + 0.5f, ox + pill_w - 0.5f, oy + pill_h - 0.5f),
            corner_radius, corner_radius
        );

        if (bg_brush) rt->FillRoundedRectangle(&pill_rect, bg_brush);
        if (border_brush) rt->DrawRoundedRectangle(&pill_rect, border_brush, 1.0f);

        // 2. Music Icon on the left
        float icon_size = 14.0f * dpi_scale;
        float icon_pad_x = 10.0f * dpi_scale;
        float icon_x = ox + icon_pad_x;
        float icon_y = oy + (pill_h - icon_size) / 2.0f;
        if (white_brush) {
            DrawMusicNote(rt, white_brush, icon_x, icon_y, icon_size);
        }

        // 3. Compact vs Expanded presentation
        float compact_w = (float)s_config.compact_width * dpi_scale;
        float expanded_w = (float)s_config.expanded_width * dpi_scale;
        float expand_ratio = (pill_w - compact_w) / (expanded_w - compact_w);
        if (expand_ratio < 0.0f) expand_ratio = 0.0f;
        if (expand_ratio > 1.0f) expand_ratio = 1.0f;

        // Right-side Play/Pause icon
        float control_size = 12.0f * dpi_scale;
        float control_pad_x = 12.0f * dpi_scale;
        float control_x = ox + pill_w - control_pad_x - control_size;
        float control_y = oy + (pill_h - control_size) / 2.0f;

        bool is_playing = (s_current_media_state.status == TEMediaPlaybackStatus::Playing);
        if (white_brush) {
            if (is_playing) {
                DrawPauseIcon(rt, white_brush, control_x, control_y, control_size);
            } else {
                DrawPlayIcon(rt, white_brush, control_x, control_y, control_size);
            }
        }

        // 4. Middle Typography (fades in smoothly as pill expands)
        if (expand_ratio > 0.15f && s_text_format_title && s_text_format_artist) {
            float text_left = icon_x + icon_size + 8.0f * dpi_scale;
            float text_right = control_x - 8.0f * dpi_scale;
            if (text_right > text_left + 12.0f * dpi_scale) {
                float line_h = (pill_h - 6.0f * dpi_scale) / 2.0f;
                D2D1_RECT_F title_rect = D2D1::RectF(text_left, oy + 3.0f * dpi_scale, text_right, oy + 3.0f * dpi_scale + line_h);
                D2D1_RECT_F artist_rect = D2D1::RectF(text_left, oy + 3.0f * dpi_scale + line_h, text_right, oy + pill_h - 3.0f * dpi_scale);

                const wchar_t* title_str = s_current_media_state.title;
                if (!title_str || title_str[0] == L'\0') title_str = L"Now Playing";

                const wchar_t* artist_str = s_current_media_state.artist;
                if (!artist_str || artist_str[0] == L'\0') artist_str = s_current_media_state.album;
                if (!artist_str || artist_str[0] == L'\0') artist_str = L"Media Audio";

                ID2D1SolidColorBrush* text_title_brush = nullptr;
                ID2D1SolidColorBrush* text_artist_brush = nullptr;
                rt->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.95f, 0.95f * expand_ratio), &text_title_brush);
                rt->CreateSolidColorBrush(D2D1::ColorF(0.70f, 0.70f, 0.74f, 0.85f * expand_ratio), &text_artist_brush);

                if (text_title_brush) {
                    rt->DrawText(title_str, (UINT32)wcslen(title_str), s_text_format_title, &title_rect, text_title_brush);
                    text_title_brush->Release();
                }
                if (text_artist_brush) {
                    rt->DrawText(artist_str, (UINT32)wcslen(artist_str), s_text_format_artist, &artist_rect, text_artist_brush);
                    text_artist_brush->Release();
                }
            }
        }

        SafeRelease(bg_brush);
        SafeRelease(border_brush);
        SafeRelease(white_brush);
        SafeRelease(muted_brush);

        rt->EndDraw();
        rt->Release();
    }

    dxgi_surface->Release();
    s_surface->EndDraw();

    s_rendered_width = s_current_width;
    s_rendered_opacity = s_current_opacity;

    if (s_dcomp_device) {
        s_dcomp_device->Commit();
    }
}

// Public C ABI Implementations

BOOL TE_DynamicIslandInit(const TE_DynamicIslandConfig* config) {
    if (config) {
        s_config = *config;
    } else {
        s_config.enabled = FALSE;
        s_config.padding_tray = 12;
        s_config.compact_width = 80;
        s_config.expanded_width = 240;
        s_config.height = 30;
        s_config.corner_radius = 15.0f;
        s_config.announce_duration_ms = 3000;
        s_config.expand_duration_ms = 250;
        s_config.collapse_duration_ms = 250;
        s_config.show_idle_pill = FALSE;
    }
    s_initialized = TRUE;
    return TRUE;
}

BOOL TE_DynamicIslandEnable(HWND primary_taskbar, uint32_t dpi) {
    s_enabled = TRUE;
    s_primary_taskbar = primary_taskbar;
    s_dpi = (dpi > 0) ? dpi : 96;

    float dpi_scale = (float)s_dpi / 96.0f;
    s_current_width = (float)s_config.compact_width * dpi_scale;
    s_target_width = s_current_width;

    if (s_root_visual && s_dcomp_device && s_d2d_factory) {
        InternalAttachVisuals();
    }

    if (!s_media_source) {
        s_owned_media_source = CreateGsmtcMediaSource();
        s_media_source = s_owned_media_source.get();
        if (s_media_source) {
            s_media_source->Initialize();
        }
    }

    if (s_media_source && s_wake_callback) {
        s_media_source->SetWakeCallback(s_wake_callback);
    }

    if (s_media_source) {
        TEMediaStateSnapshot snap = {};
        if (s_media_source->GetCurrentSnapshot(&snap)) {
            s_current_media_state = snap;
            s_last_track_change_id = snap.track_change_id;
            s_state_dirty = true;
        }
    }

    UpdateLayoutAndBounds();
    if (s_wake_callback) {
        s_wake_callback();
    }
    return TRUE;
}

void TE_DynamicIslandDisable(void) {
    s_enabled = FALSE;
    s_target_opacity = 0.0f;
    s_current_opacity = 0.0f;
    s_is_hovered = false;
    s_primary_taskbar = NULL;
    s_tray_hwnd = NULL;
    s_headroom_y = 120.0f;

    float dpi_scale = (s_dpi > 0) ? ((float)s_dpi / 96.0f) : 1.0f;
    s_current_width = (float)s_config.compact_width * dpi_scale;
    s_target_width = s_current_width;

    if (s_island_effect) {
        s_island_effect->SetOpacity(0.0f);
    }

    if (s_owned_media_source) {
        s_owned_media_source->Shutdown();
        s_owned_media_source.reset();
    }
    s_media_source = nullptr;

    if (s_root_visual && s_island_visual) {
        s_root_visual->RemoveVisual(s_island_visual);
    }
    SafeRelease(s_surface);
    SafeRelease(s_island_effect);
    SafeRelease(s_translate_transform);
    SafeRelease(s_island_visual);

    SafeRelease(s_ellipsis_title);
    SafeRelease(s_ellipsis_artist);
    SafeRelease(s_text_format_title);
    SafeRelease(s_text_format_artist);
    SafeRelease(s_dwrite_factory);

    s_surface_width = 0;
    s_surface_height = 0;
}

void TE_DynamicIslandShutdown(void) {
    TE_DynamicIslandDisable();
    TE_DynamicIslandDetachVisualTree();
    s_initialized = FALSE;
}

void TE_DynamicIslandUpdateConfig(const TE_DynamicIslandConfig* config) {
    if (!config) return;
    s_config = *config;
    float dpi_scale = (s_dpi > 0) ? ((float)s_dpi / 96.0f) : 1.0f;
    if (!s_is_hovered && GetTickCount64() >= s_announce_end_time_ms) {
        s_target_width = (float)s_config.compact_width * dpi_scale;
        s_current_width = s_target_width;
    }
    UpdateLayoutAndBounds();
    s_state_dirty = true;
}

BOOL TE_DynamicIslandIsEnabled(void) {
    return s_enabled ? TRUE : FALSE;
}

BOOL TE_DynamicIslandIsVisible(void) {
    return (s_enabled && s_current_opacity > 0.001f) ? TRUE : FALSE;
}

BOOL TE_DynamicIslandIsDirty(void) {
    BOOL dirty = s_state_dirty ? TRUE : FALSE;
    s_state_dirty = false;
    return dirty;
}

void TE_DynamicIslandOnGeometryChanged(const RECT* taskbar_rect, HWND tray_hwnd) {
    if (taskbar_rect) {
        s_taskbar_rect = *taskbar_rect;
    }
    s_tray_hwnd = tray_hwnd;
    UpdateLayoutAndBounds();
    s_state_dirty = true;
}

void TE_DynamicIslandOnDpiChanged(uint32_t dpi) {
    if (dpi == 0) return;
    float old_scale = (s_dpi > 0) ? ((float)s_dpi / 96.0f) : 1.0f;
    s_dpi = dpi;
    float new_scale = (float)s_dpi / 96.0f;

    if (old_scale > 0.0f) {
        s_current_width = (s_current_width / old_scale) * new_scale;
        s_target_width = (s_target_width / old_scale) * new_scale;
    } else {
        s_current_width = (float)s_config.compact_width * new_scale;
        s_target_width = s_current_width;
    }

    EnsureDirectWrite();
    EnsureSurface();
    UpdateLayoutAndBounds();
    s_state_dirty = true;
}

static BOOL InternalAttachVisuals() {
    if (!s_root_visual || !s_dcomp_device || !s_d2d_factory) return FALSE;

    HRESULT hr = S_OK;

    if (!s_island_visual) {
        hr = s_dcomp_device->CreateVisual(&s_island_visual);
        if (FAILED(hr) || !s_island_visual) return FALSE;
    }

    if (!s_translate_transform) {
        hr = s_dcomp_device->CreateTranslateTransform(&s_translate_transform);
        if (SUCCEEDED(hr) && s_translate_transform) {
            s_island_visual->SetTransform(s_translate_transform);
        }
    }

    if (!s_island_effect) {
        hr = s_dcomp_device->CreateEffectGroup(&s_island_effect);
        if (SUCCEEDED(hr) && s_island_effect) {
            s_island_effect->SetOpacity(s_current_opacity);
            s_island_visual->SetEffect(s_island_effect);
        }
    }

    EnsureDirectWrite();
    EnsureSurface();
    UpdateLayoutAndBounds();

    s_root_visual->RemoveVisual(s_island_visual);
    hr = s_root_visual->AddVisual(s_island_visual, TRUE, nullptr);
    if (FAILED(hr)) return FALSE;

    TE_DCompAttachIslandVisual(s_island_visual);
    if (s_dcomp_device) {
        s_dcomp_device->Commit();
    }

    s_state_dirty = true;
    return TRUE;
}

BOOL TE_DynamicIslandAttachVisualTree(struct IDCompositionVisual* root_visual, struct IDCompositionDevice* dcomp_device, struct ID2D1Factory* d2d_factory) {
    if (!root_visual || !dcomp_device || !d2d_factory) return FALSE;

    s_root_visual = root_visual;
    s_dcomp_device = dcomp_device;
    s_d2d_factory = d2d_factory;

    if (!s_enabled || !s_config.enabled) {
        return TRUE;
    }

    return InternalAttachVisuals();
}

void TE_DynamicIslandDetachVisualTree(void) {
    TE_DCompDetachIslandVisual(s_island_visual);
    if (s_root_visual && s_island_visual) {
        s_root_visual->RemoveVisual(s_island_visual);
    }
    SafeRelease(s_surface);
    SafeRelease(s_island_effect);
    SafeRelease(s_translate_transform);
    SafeRelease(s_island_visual);

    SafeRelease(s_ellipsis_title);
    SafeRelease(s_ellipsis_artist);
    SafeRelease(s_text_format_title);
    SafeRelease(s_text_format_artist);
    SafeRelease(s_dwrite_factory);

    s_root_visual = nullptr;
    s_dcomp_device = nullptr;
    s_d2d_factory = nullptr;
    s_surface_width = 0;
    s_surface_height = 0;
}

void TE_DynamicIslandUpdateFrame(float dt_sec) {
    if (!s_enabled) return;

    if (std::isnan(dt_sec) || dt_sec <= 0.0f || dt_sec > 0.1f) {
        dt_sec = 0.016f;
    }

    // 1. Poll media state from bridge
    if (s_media_source) {
        if (s_media_source->HasNewState()) {
            TEMediaStateSnapshot snap = {};
            if (s_media_source->GetCurrentSnapshot(&snap)) {
                if (snap.track_change_id != s_last_track_change_id) {
                    s_last_track_change_id = snap.track_change_id;
                    if (snap.has_media && snap.status == TEMediaPlaybackStatus::Playing) {
                        s_announce_end_time_ms = GetTickCount64() + (uint64_t)s_config.announce_duration_ms;
                        if (s_wake_callback) {
                            s_wake_callback();
                        }
                    }
                }
                s_current_media_state = snap;
                s_state_dirty = true;
            }
        }
    }

    // 2. Evaluate target opacity & sizing state
    bool media_active = s_enabled && s_current_media_state.has_media &&
        (s_current_media_state.status == TEMediaPlaybackStatus::Playing ||
         s_current_media_state.status == TEMediaPlaybackStatus::Paused);

    if (!media_active) {
        if (s_config.show_idle_pill || s_is_hovered) {
            s_target_opacity = 1.0f;
        } else {
            s_target_opacity = 0.0f;
        }
    } else {
        s_target_opacity = 1.0f;
    }

    float dpi_scale = (s_dpi > 0) ? ((float)s_dpi / 96.0f) : 1.0f;
    uint64_t now = GetTickCount64();
    bool is_announcing = (now < s_announce_end_time_ms);

    if (media_active && (s_is_hovered || is_announcing)) {
        s_target_width = (float)s_config.expanded_width * dpi_scale;
    } else {
        s_target_width = (float)s_config.compact_width * dpi_scale;
    }

    // 3. Smooth Opacity Interpolation
    if (s_current_opacity != s_target_opacity) {
        float alpha_speed = 12.0f;
        float factor = 1.0f - std::exp(-alpha_speed * dt_sec);
        s_current_opacity += (s_target_opacity - s_current_opacity) * factor;
        if (std::abs(s_target_opacity - s_current_opacity) < 0.005f) {
            s_current_opacity = s_target_opacity;
        }
        if (s_island_effect) {
            s_island_effect->SetOpacity(s_current_opacity);
        }
        s_state_dirty = true;
    }

    // 4. Smooth Size Interpolation
    if (s_current_width != s_target_width) {
        float duration_ms = (s_target_width > s_current_width) ?
            (float)s_config.expand_duration_ms : (float)s_config.collapse_duration_ms;
        if (duration_ms <= 0.0f) duration_ms = 250.0f;
        float width_speed = (1000.0f / duration_ms) * 3.0f;
        float factor = 1.0f - std::exp(-width_speed * dt_sec);
        s_current_width += (s_target_width - s_current_width) * factor;
        if (std::abs(s_target_width - s_current_width) < 0.5f) {
            s_current_width = s_target_width;
        }
        UpdateLayoutAndBounds();
        s_state_dirty = true;
    }

    // 5. Redraw surface if dirty and visible
    if (s_state_dirty && s_current_opacity > 0.001f) {
        RenderIslandSurface();
        s_state_dirty = false;
    }
}

void TE_DynamicIslandOnMouseMove(float cursor_x, float cursor_y) {
    if (!s_enabled) {
        if (s_is_hovered) {
            s_is_hovered = false;
            s_state_dirty = true;
        }
        return;
    }
    POINT pt = { (LONG)cursor_x, (LONG)cursor_y };
    bool was_hovered = s_is_hovered;
    s_is_hovered = (PtInRect(&s_hit_rect, pt) != FALSE);
    if (s_is_hovered != was_hovered) {
        s_state_dirty = true;
    }
}

void TE_DynamicIslandOnMouseLeave(void) {
    if (s_is_hovered) {
        s_is_hovered = false;
        s_state_dirty = true;
    }
}

BOOL TE_DynamicIslandCheckClick(float cursor_x, float cursor_y) {
    if (!TE_DynamicIslandIsVisible()) return FALSE;
    POINT pt = { (LONG)cursor_x, (LONG)cursor_y };
    if (PtInRect(&s_hit_rect, pt)) {
        if (s_media_source) {
            s_media_source->TogglePlayPauseAsync();
        }
        return TRUE;
    }
    return FALSE;
}

// Queries for Unit Testing & Verification

BOOL TE_DynamicIslandGetBounds(RECT* out_screen_rect) {
    if (!out_screen_rect) return FALSE;
    *out_screen_rect = s_hit_rect;
    return TRUE;
}

void TE_DynamicIslandGetVisualOffset(float* out_x, float* out_y) {
    if (out_x) *out_x = s_visual_x;
    if (out_y) *out_y = s_visual_y;
}

float TE_DynamicIslandGetCurrentWidth(void) {
    return s_current_width;
}

float TE_DynamicIslandGetCurrentOpacity(void) {
    return s_current_opacity;
}

BOOL TE_DynamicIslandIsSettled(void) {
    if (!s_enabled) return TRUE;
    uint64_t now = GetTickCount64();
    if (now < s_announce_end_time_ms) return FALSE;
    if (std::abs(s_current_width - s_target_width) >= 0.5f) return FALSE;
    if (std::abs(s_current_opacity - s_target_opacity) >= 0.005f) return FALSE;
    if (s_state_dirty) return FALSE;
    return TRUE;
}

void TE_DynamicIslandSetHeadroom(float headroom_y) {
    s_headroom_y = headroom_y;
    UpdateLayoutAndBounds();
}

void TE_DynamicIslandSetWakeCallback(TE_DynamicIslandWakeCallback callback) {
    s_wake_callback = callback;
    if (s_media_source) {
        s_media_source->SetWakeCallback(callback);
    }
}

void TE_DynamicIslandSetMediaSource(IMediaSource* source) {
    if (s_owned_media_source) {
        s_owned_media_source->Shutdown();
        s_owned_media_source.reset();
    }
    s_media_source = source;
    if (s_media_source && s_wake_callback) {
        s_media_source->SetWakeCallback(s_wake_callback);
    }
}
