/**
 * @file dcomp_overlay.cpp
 * @brief DirectComposition overlay window and visual tree for IconHover.
 *
 * Creates a transparent child window of Shell_TrayWnd and initializes
 * a DirectComposition device with a visual tree containing per-icon
 * scale and translate transforms. Icon bitmaps are drawn to DComp
 * surfaces via GDI.
 *
 * The visual tree hierarchy:
 *   IDCompositionTarget (bound to overlay HWND)
 *   └── Root Visual
 *       ├── Icon Visual 0 (surface + scale + translate transforms)
 *       ├── Icon Visual 1
 *       └── ...
 */

#include "dcomp_overlay.h"
#include "icon_hover_internal.h"
#include <sdk/te_log.h>
#include "dynamic_island.h"

static IDCompositionVisual* s_island_visual_target0 = nullptr;

#include <windows.h>
#include <dcomp.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1_2.h>
#include <d2d1_3.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <stdio.h>
#include <math.h>
#include <d3d11.h>
#include <emmintrin.h>

#ifdef _MSC_VER
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

static const char* LOG_TAG = "DCompOverlay";
// Use "TaskbarEngineHoverOverlay" class name so LiveWallpaper ignores this window for occlusion and pause logic.
static const wchar_t* OVERLAY_CLASS_NAME = L"TaskbarEngineHoverOverlay";

/** DComp device and visual tree state. */
static ID2D1Factory* s_d2d_factory = nullptr;
static ID3D11Device* s_d3d_device = nullptr;
static IDCompositionDevice* s_dcomp_device = nullptr;

struct TE_TargetVisualTree {
    HWND taskbar_hwnd;
    HWND overlay_hwnd;
    IDCompositionTarget* dcomp_target;
    IDCompositionVisual* root_visual;
    IDCompositionEffectGroup* root_effect;

    IDCompositionVisual* icon_visuals[TE_HOVER_MAX_ICONS];
    IDCompositionScaleTransform* scale_transforms[TE_HOVER_MAX_ICONS];
    IDCompositionTranslateTransform* translate_transforms[TE_HOVER_MAX_ICONS];
    IDCompositionMatrixTransform3D* matrix_transforms[TE_HOVER_MAX_ICONS];
    IDCompositionEffectGroup* icon_effect_groups[TE_HOVER_MAX_ICONS];
    bool icon_is_start[TE_HOVER_MAX_ICONS];
    D2D1_POINT_2F icon_centers[TE_HOVER_MAX_ICONS];
    IDCompositionSurface* icon_surfaces[TE_HOVER_MAX_ICONS];
    int visual_count;
    RECT start_button_bounds;
    bool is_active;

    /* PERF-302: Identity matrix tracking */
    bool icon_is_identity_matrix[TE_HOVER_MAX_ICONS];
    /* PERF-202/203: Surface reuse cache */
    HBITMAP cached_bitmaps[TE_HOVER_MAX_ICONS];
    int cached_surface_w[TE_HOVER_MAX_ICONS];
    int cached_surface_h[TE_HOVER_MAX_ICONS];
};

static TE_TargetVisualTree s_targets[TE_MAX_DCOMP_TARGETS] = {};
static int s_target_count = 0;

/** Custom Start Button state */
static ID2D1Bitmap* s_start_button_bitmap = nullptr;
static int s_custom_start_enabled = 0;
static RECT s_start_button_bounds = {};
static wchar_t s_cached_start_path[MAX_PATH] = {};

static void* s_start_pixel_data = nullptr;
static UINT s_start_pixel_w = 0;
static UINT s_start_pixel_h = 0;
static UINT s_start_pixel_stride = 0;

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

static HWND s_overlay_hwnd_ref = NULL;

/** Window class registration flag. */
static BOOL s_class_registered = FALSE;

/** Overlay window procedure — transparent to all mouse input. */
static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND TE_DCompCreateOverlayWindow(HWND taskbar_hwnd, int x, int y, int width, int height)
{
    if (!taskbar_hwnd) return NULL;

    HINSTANCE hinstance = (HINSTANCE)GetWindowLongPtrW(taskbar_hwnd, GWLP_HINSTANCE);
    if (!hinstance) {
        hinstance = GetModuleHandleW(NULL);
    }

    /* Register overlay window class once */
    if (!s_class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = hinstance;
        wc.lpszClassName = OVERLAY_CLASS_NAME;
        wc.style = CS_HREDRAW | CS_VREDRAW;

        if (!RegisterClassExW(&wc)) {
            /* Class may already be registered from a previous enable/disable cycle */
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to register overlay window class");
                return NULL;
            }
        }
        s_class_registered = TRUE;
    }

    HWND overlay = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS_NAME,
        NULL,
        WS_POPUP | WS_VISIBLE,
        x, y, width, height,
        taskbar_hwnd, /* Establish ownership so overlay always stays above taskbar */
        NULL,
        hinstance,
        NULL
    );

    if (!overlay) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create overlay window");
        return NULL;
    }

    SetLayeredWindowAttributes(overlay, 0, 255, LWA_ALPHA);
    s_overlay_hwnd_ref = overlay;

    char msg[128];
    snprintf(msg, sizeof(msg), "Overlay window created: %dx%d at (%d,%d)", width, height, x, y);
    TE_LogWrite(TE_LOG_INFO, LOG_TAG, msg);

    return overlay;
}

void TE_DCompMoveOverlayWindow(HWND overlay_hwnd, int x, int y, int width, int height)
{
    if (overlay_hwnd && IsWindow(overlay_hwnd)) {
        SetWindowPos(overlay_hwnd, HWND_TOPMOST, x, y, width, height,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    }
}

void TE_DCompDestroyOverlayWindow(HWND overlay_hwnd)
{
    if (overlay_hwnd && IsWindow(overlay_hwnd)) {
        for (int i = 0; i < s_target_count; i++) {
            if (s_targets[i].overlay_hwnd == overlay_hwnd) {
                TE_DCompRemoveTarget(i);
                break;
            }
        }
        if (s_overlay_hwnd_ref == overlay_hwnd) {
            s_overlay_hwnd_ref = NULL;
        }
        DestroyWindow(overlay_hwnd);
        TE_LogWrite(TE_LOG_INFO, LOG_TAG, "Overlay window destroyed");
    }
}

HRESULT TE_DCompInitDevice(HWND overlay_hwnd)
{
    if (!overlay_hwnd) return TE_E_INVALIDARG;

    s_overlay_hwnd_ref = overlay_hwnd;

    /* Create D2D factory */
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory), (void**)&s_d2d_factory);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create D2D factory");
        return TE_E_FAIL;
    }

    /* Create D3D11 device with BGRA support for D2D interop */
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        nullptr, 0, D3D11_SDK_VERSION, &s_d3d_device, nullptr, nullptr
    );
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create D3D11 device");
        return TE_E_FAIL;
    }

    /* Extract DXGI device */
    IDXGIDevice* dxgi_device = nullptr;
    hr = s_d3d_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to get DXGI device");
        return TE_E_FAIL;
    }

    /* Create DComp device using the DXGI device */
    hr = DCompositionCreateDevice(dxgi_device, __uuidof(IDCompositionDevice), (void**)&s_dcomp_device);
    dxgi_device->Release();
    
    if (FAILED(hr) || !s_dcomp_device) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "DCompositionCreateDevice failed");
        return TE_E_FAIL;
    }

    /* Create primary target (index 0) bound to overlay window */
    hr = s_dcomp_device->CreateTargetForHwnd(overlay_hwnd, TRUE, &s_targets[0].dcomp_target);
    if (FAILED(hr) || !s_targets[0].dcomp_target) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "CreateTargetForHwnd failed");
        s_dcomp_device->Release();
        s_dcomp_device = nullptr;
        return TE_E_FAIL;
    }

    /* Create root visual for target 0 */
    hr = s_dcomp_device->CreateVisual(&s_targets[0].root_visual);
    if (FAILED(hr) || !s_targets[0].root_visual) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create root visual");
        s_targets[0].dcomp_target->Release();
        s_targets[0].dcomp_target = nullptr;
        s_dcomp_device->Release();
        s_dcomp_device = nullptr;
        return TE_E_FAIL;
    }

    /* Bind root visual to target */
    hr = s_targets[0].dcomp_target->SetRoot(s_targets[0].root_visual);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to set root visual");
        s_targets[0].root_visual->Release();
        s_targets[0].root_visual = nullptr;
        s_targets[0].dcomp_target->Release();
        s_targets[0].dcomp_target = nullptr;
        s_dcomp_device->Release();
        s_dcomp_device = nullptr;
        return TE_E_FAIL;
    }

    /* Create root effect group for opacity */
    hr = s_dcomp_device->CreateEffectGroup(&s_targets[0].root_effect);
    if (SUCCEEDED(hr) && s_targets[0].root_effect) {
        s_targets[0].root_effect->SetOpacity(0.0f);
        s_targets[0].root_visual->SetEffect(s_targets[0].root_effect);
    }
    s_targets[0].overlay_hwnd = overlay_hwnd;
    s_targets[0].is_active = true;
    s_target_count = 1;

    s_dcomp_device->Commit();

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "DComp device initialized successfully");
    return TE_S_OK;
}

int TE_DCompGetTargetCount(void)
{
    return s_target_count;
}

HRESULT TE_DCompAddTarget(HWND taskbar_hwnd, HWND overlay_hwnd, int* out_target_index)
{
    if (!overlay_hwnd) return TE_E_INVALIDARG;
    if (!s_dcomp_device) return TE_E_FAIL;

    for (int i = 0; i < s_target_count; i++) {
        if (s_targets[i].is_active && s_targets[i].overlay_hwnd == overlay_hwnd) {
            s_targets[i].taskbar_hwnd = taskbar_hwnd;
            if (out_target_index) *out_target_index = i;
            return TE_S_OK;
        }
    }

    int idx = -1;
    for (int i = 0; i < s_target_count; i++) {
        if (!s_targets[i].is_active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (s_target_count >= TE_MAX_DCOMP_TARGETS) return TE_E_FAIL;
        idx = s_target_count++;
    }

    TE_TargetVisualTree* t = &s_targets[idx];
    memset(t, 0, sizeof(*t));
    t->taskbar_hwnd = taskbar_hwnd;
    t->overlay_hwnd = overlay_hwnd;

    HRESULT hr = s_dcomp_device->CreateTargetForHwnd(overlay_hwnd, TRUE, &t->dcomp_target);
    if (FAILED(hr) || !t->dcomp_target) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "CreateTargetForHwnd failed for secondary target");
        return TE_E_FAIL;
    }

    hr = s_dcomp_device->CreateVisual(&t->root_visual);
    if (FAILED(hr) || !t->root_visual) {
        t->dcomp_target->Release();
        t->dcomp_target = nullptr;
        return TE_E_FAIL;
    }

    t->dcomp_target->SetRoot(t->root_visual);

    hr = s_dcomp_device->CreateEffectGroup(&t->root_effect);
    if (SUCCEEDED(hr) && t->root_effect) {
        t->root_effect->SetOpacity(0.0f);
        t->root_visual->SetEffect(t->root_effect);
    }

    t->is_active = true;
    if (out_target_index) *out_target_index = idx;
    s_dcomp_device->Commit();
    return TE_S_OK;
}

static void ReleaseVisualTreeForTarget(int target_idx)
{
    if (target_idx < 0 || target_idx >= TE_MAX_DCOMP_TARGETS) return;
    TE_TargetVisualTree* t = &s_targets[target_idx];
    if (t->root_visual) {
        t->root_visual->RemoveAllVisuals();
    }
    for (int i = 0; i < TE_HOVER_MAX_ICONS; i++) {
        if (t->icon_effect_groups[i]) { t->icon_effect_groups[i]->Release(); t->icon_effect_groups[i] = nullptr; }
        if (t->matrix_transforms[i]) { t->matrix_transforms[i]->Release(); t->matrix_transforms[i] = nullptr; }
        if (t->icon_surfaces[i]) { t->icon_surfaces[i]->Release(); t->icon_surfaces[i] = nullptr; }
        if (t->translate_transforms[i]) { t->translate_transforms[i]->Release(); t->translate_transforms[i] = nullptr; }
        if (t->scale_transforms[i]) { t->scale_transforms[i]->Release(); t->scale_transforms[i] = nullptr; }
        if (t->icon_visuals[i]) { t->icon_visuals[i]->Release(); t->icon_visuals[i] = nullptr; }
        t->icon_is_start[i] = false;
        t->cached_bitmaps[i] = NULL;
        t->cached_surface_w[i] = 0;
        t->cached_surface_h[i] = 0;
        t->icon_is_identity_matrix[i] = false;
    }
    t->visual_count = 0;
}

static void ReleaseVisualTree(void)
{
    for (int i = 0; i < s_target_count; i++) {
        ReleaseVisualTreeForTarget(i);
    }
}

void TE_DCompRemoveTarget(int target_index)
{
    if (target_index < 0 || target_index >= TE_MAX_DCOMP_TARGETS) return;
    TE_TargetVisualTree* t = &s_targets[target_index];
    if (!t->is_active) return;

    if (target_index == 0) {
        TE_DynamicIslandDetachVisualTree();
        s_island_visual_target0 = nullptr;
    }

    ReleaseVisualTreeForTarget(target_index);
    if (t->root_effect) { t->root_effect->Release(); t->root_effect = nullptr; }
    if (t->root_visual) { t->root_visual->Release(); t->root_visual = nullptr; }
    if (t->dcomp_target) { t->dcomp_target->Release(); t->dcomp_target = nullptr; }
    t->is_active = false;
    t->overlay_hwnd = NULL;
    t->taskbar_hwnd = NULL;
}

void TE_DCompDestroyDevice(void)
{
    TE_DynamicIslandDetachVisualTree();
    s_island_visual_target0 = nullptr;

    ReleaseVisualTree();

    for (int i = 0; i < s_target_count; i++) {
        if (s_targets[i].root_effect) { s_targets[i].root_effect->Release(); s_targets[i].root_effect = nullptr; }
        if (s_targets[i].root_visual) { s_targets[i].root_visual->Release(); s_targets[i].root_visual = nullptr; }
        if (s_targets[i].dcomp_target) { s_targets[i].dcomp_target->Release(); s_targets[i].dcomp_target = nullptr; }
        s_targets[i].is_active = false;
    }
    s_target_count = 0;

    if (s_start_button_bitmap) { s_start_button_bitmap->Release(); s_start_button_bitmap = nullptr; }
    if (s_start_pixel_data) { free(s_start_pixel_data); s_start_pixel_data = nullptr; s_start_pixel_w = 0; s_start_pixel_h = 0; s_start_pixel_stride = 0; }

    if (s_dcomp_device) { s_dcomp_device->Release(); s_dcomp_device = nullptr; }
    if (s_d3d_device) { s_d3d_device->Release(); s_d3d_device = nullptr; }
    if (s_d2d_factory) { s_d2d_factory->Release(); s_d2d_factory = nullptr; }

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "DComp device destroyed");
}

HRESULT TE_DCompBuildVisualTreeForTarget(int target_index, int count, const TE_IconElementInfo* elements, const HBITMAP* bitmaps, int baseline_y, int overlay_x, int overlay_y)
{
    if (!s_dcomp_device) return TE_E_FAIL;
    if (target_index < 0 || target_index >= TE_MAX_DCOMP_TARGETS) return TE_E_INVALIDARG;
    TE_TargetVisualTree* t = &s_targets[target_index];
    if (!t->is_active || !t->root_visual) return TE_E_FAIL;
    if (count <= 0 || !elements) return TE_E_INVALIDARG;
    if (count > TE_HOVER_MAX_ICONS) count = TE_HOVER_MAX_ICONS;

    /* PERF-202/203: Do not tear down visual tree. Reuse existing visual nodes and surfaces. */
    HRESULT hr;
    int baseline_OVERLAY = baseline_y - overlay_y;

    for (int i = 0; i < count; i++) {
        /* 1. Ensure visual node, transforms and effect group exist for slot i */
        if (!t->icon_visuals[i]) {
            hr = s_dcomp_device->CreateVisual(&t->icon_visuals[i]);
            if (FAILED(hr)) continue;

            hr = s_dcomp_device->CreateScaleTransform(&t->scale_transforms[i]);
            if (FAILED(hr)) {
                t->icon_visuals[i]->Release();
                t->icon_visuals[i] = nullptr;
                continue;
            }

            hr = s_dcomp_device->CreateTranslateTransform(&t->translate_transforms[i]);
            if (FAILED(hr)) {
                t->scale_transforms[i]->Release();
                t->scale_transforms[i] = nullptr;
                t->icon_visuals[i]->Release();
                t->icon_visuals[i] = nullptr;
                continue;
            }

            IDCompositionTransform* transforms[2] = {
                t->scale_transforms[i],
                t->translate_transforms[i]
            };
            IDCompositionTransform* group = nullptr;
            hr = s_dcomp_device->CreateTransformGroup(transforms, 2, &group);
            if (SUCCEEDED(hr) && group) {
                t->icon_visuals[i]->SetTransform(group);
                group->Release();
            }

            hr = s_dcomp_device->CreateMatrixTransform3D(&t->matrix_transforms[i]);
            if (SUCCEEDED(hr) && t->matrix_transforms[i]) {
                D3DMATRIX identity = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
                t->matrix_transforms[i]->SetMatrix(identity);
                t->icon_is_identity_matrix[i] = true;
            }

            hr = s_dcomp_device->CreateEffectGroup(&t->icon_effect_groups[i]);
            if (SUCCEEDED(hr) && t->icon_effect_groups[i]) {
                if (t->matrix_transforms[i]) {
                    t->icon_effect_groups[i]->SetTransform3D(t->matrix_transforms[i]);
                }
                t->icon_effect_groups[i]->SetOpacity(0.0f);
                t->icon_visuals[i]->SetEffect(t->icon_effect_groups[i]);
            } else if (t->matrix_transforms[i]) {
                t->icon_visuals[i]->SetEffect(t->matrix_transforms[i]);
            }

            /* Add as child of root visual */
            if (i == 0) {
                t->root_visual->AddVisual(t->icon_visuals[i], TRUE, nullptr);
            } else if (t->icon_visuals[i - 1]) {
                t->root_visual->AddVisual(t->icon_visuals[i], TRUE, t->icon_visuals[i - 1]);
            }
        }

        /* 2. Reset initial transform values (identity) */
        t->scale_transforms[i]->SetScaleX(1.0f);
        t->scale_transforms[i]->SetScaleY(1.0f);
        t->translate_transforms[i]->SetOffsetX(0.0f);
        t->translate_transforms[i]->SetOffsetY(0.0f);

        /* 3. Calculate positions using glyphCenter and taskbar baseline */
        const RECT* btn = &elements[i].buttonRect;
        const RECT* gl = &elements[i].glyphRect;

        float w_surf = (float)(btn->right - btn->left);
        float h_surf = (float)(btn->bottom - btn->top);

        float glyphCenterX_OVERLAY = ((float)(gl->left + gl->right) / 2.0f) - (float)overlay_x;
        float glyphCenterY_OVERLAY = ((float)(gl->top + gl->bottom) / 2.0f) - (float)overlay_y;

        float X_visual_base = glyphCenterX_OVERLAY - (w_surf / 2.0f);
        float Y_visual_base = glyphCenterY_OVERLAY - (h_surf / 2.0f);

        float c_x = w_surf / 2.0f;
        float c_y = (float)baseline_OVERLAY - Y_visual_base;

        t->scale_transforms[i]->SetCenterX(c_x);
        t->scale_transforms[i]->SetCenterY(c_y);
        t->icon_visuals[i]->SetOffsetX(X_visual_base);
        t->icon_visuals[i]->SetOffsetY(Y_visual_base);

        t->icon_centers[i].x = w_surf / 2.0f;
        t->icon_centers[i].y = h_surf / 2.0f;

        bool is_start_button = (elements[i].element_type == TE_ELEM_START_BUTTON);
        t->icon_is_start[i] = is_start_button;
        if (is_start_button) {
            t->start_button_bounds = *btn;
        }

        /* 4. Surface reuse or creation (PERF-202/203) */
        if (is_start_button || (bitmaps && bitmaps[i])) {
            int bmp_w = (int)w_surf;
            int bmp_h = (int)h_surf;
            if (bmp_w <= 0) bmp_w = 48;
            if (bmp_h <= 0) bmp_h = 48;

            HBITMAP current_hbmp = (bitmaps && bitmaps[i]) ? bitmaps[i] : NULL;
            bool surface_matches = (t->icon_surfaces[i] != nullptr &&
                                    t->cached_surface_w[i] == bmp_w &&
                                    t->cached_surface_h[i] == bmp_h &&
                                    t->cached_bitmaps[i] == current_hbmp &&
                                    !is_start_button);

            if (!surface_matches) {
                if (t->icon_surfaces[i] && (t->cached_surface_w[i] != bmp_w || t->cached_surface_h[i] != bmp_h)) {
                    t->icon_surfaces[i]->Release();
                    t->icon_surfaces[i] = nullptr;
                }

                if (!t->icon_surfaces[i]) {
                    hr = s_dcomp_device->CreateSurface(
                        (UINT)bmp_w, (UINT)bmp_h,
                        DXGI_FORMAT_B8G8R8A8_UNORM,
                        DXGI_ALPHA_MODE_PREMULTIPLIED,
                        &t->icon_surfaces[i]
                    );
                }

                if (t->icon_surfaces[i]) {
                    IDXGISurface* dxgi_surface = nullptr;
                    POINT offset_point = {};
                    hr = t->icon_surfaces[i]->BeginDraw(NULL, __uuidof(IDXGISurface), (void**)&dxgi_surface, &offset_point);
                    if (SUCCEEDED(hr) && dxgi_surface) {
                        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                            D2D1_RENDER_TARGET_TYPE_DEFAULT,
                            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
                        );
                        ID2D1RenderTarget* rt = nullptr;
                        hr = s_d2d_factory->CreateDxgiSurfaceRenderTarget(dxgi_surface, &props, &rt);

                        if (SUCCEEDED(hr) && rt) {
                            rt->BeginDraw();
                            rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

                            if (is_start_button && (s_start_button_bitmap || s_start_pixel_data)) {
                                ID2D1Bitmap* start_bmp = s_start_button_bitmap;
                                bool release_bmp = false;
                                if (!start_bmp && s_start_pixel_data) {
                                    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
                                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
                                    );
                                    if (SUCCEEDED(rt->CreateBitmap(D2D1::SizeU(s_start_pixel_w, s_start_pixel_h),
                                                                   s_start_pixel_data, s_start_pixel_stride,
                                                                   &bp, &start_bmp))) {
                                        release_bmp = true;
                                    }
                                }

                                if (start_bmp) {
                                    float gl_w = (float)(gl->right - gl->left);
                                    float gl_h = (float)(gl->bottom - gl->top);
                                    if (gl_w <= 0.0f) gl_w = (float)bmp_w * 0.60f;
                                    if (gl_h <= 0.0f) gl_h = (float)bmp_h * 0.60f;

                                    float dest_x = (float)offset_point.x + ((float)bmp_w - gl_w) / 2.0f;
                                    float dest_y = (float)offset_point.y + ((float)bmp_h - gl_h) / 2.0f;

                                    D2D1_RECT_F dest_rect = D2D1::RectF(
                                        dest_x, dest_y,
                                        dest_x + gl_w, dest_y + gl_h
                                    );

                                    rt->DrawBitmap(start_bmp, dest_rect, 1.0f,
                                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

                                    if (release_bmp && start_bmp) start_bmp->Release();
                                }
                            } else if (bitmaps && bitmaps[i]) {
                                DIBSECTION dib;
                                if (GetObject(bitmaps[i], sizeof(DIBSECTION), &dib) == sizeof(DIBSECTION) && dib.dsBm.bmBits) {
                                    D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
                                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
                                    );
                                    ID2D1Bitmap* d2d_bmp = nullptr;
                                    hr = rt->CreateBitmap(D2D1::SizeU(dib.dsBm.bmWidth, dib.dsBm.bmHeight),
                                                          dib.dsBm.bmBits,
                                                          dib.dsBm.bmWidthBytes,
                                                          &bmpProps,
                                                          &d2d_bmp);
                                    if (SUCCEEDED(hr) && d2d_bmp) {
                                        float dest_w = (float)dib.dsBm.bmWidth;
                                        float dest_h = (float)dib.dsBm.bmHeight;
                                        float dest_x = (float)offset_point.x + ((float)bmp_w - dest_w) / 2.0f;
                                        float dest_y = (float)offset_point.y + ((float)bmp_h - dest_h) / 2.0f;

                                        D2D1_RECT_F dest_rect = D2D1::RectF(
                                            dest_x,
                                            dest_y,
                                            dest_x + dest_w,
                                            dest_y + dest_h
                                        );
                                        rt->DrawBitmap(d2d_bmp, dest_rect);
                                        d2d_bmp->Release();
                                    }
                                }
                            }
                            rt->EndDraw();
                            rt->Release();
                        }
                        dxgi_surface->Release();
                        t->icon_surfaces[i]->EndDraw();
                    }

                    t->cached_surface_w[i] = bmp_w;
                    t->cached_surface_h[i] = bmp_h;
                    t->cached_bitmaps[i] = current_hbmp;
                    t->icon_visuals[i]->SetContent(t->icon_surfaces[i]);
                }
            }
        }

        /* 5. Set initial opacity */
        float init_opacity = is_start_button ? 1.0f : 0.0f;
        if (t->icon_effect_groups[i]) {
            t->icon_effect_groups[i]->SetOpacity(init_opacity);
        }
    }

    /* 6. Hide inactive visuals beyond count in the pool (PERF-202) */
    for (int j = count; j < TE_HOVER_MAX_ICONS; j++) {
        if (t->icon_effect_groups[j]) {
            t->icon_effect_groups[j]->SetOpacity(0.0f);
        }
        t->icon_is_start[j] = false;
    }
    t->visual_count = count;

    char msg[64];
    snprintf(msg, sizeof(msg), "Built visual tree for target %d with %d icon visuals", target_index, t->visual_count);
    TE_LogWrite(TE_LOG_INFO, LOG_TAG, msg);

    if (target_index == 0) {
        TE_DynamicIslandAttachVisualTree(t->root_visual, s_dcomp_device, s_d2d_factory);
        if (s_island_visual_target0) {
            t->root_visual->RemoveVisual(s_island_visual_target0);
            t->root_visual->AddVisual(s_island_visual_target0, TRUE, nullptr);
        }
    }

    if (s_dcomp_device) {
        s_dcomp_device->Commit();
    }

    return TE_S_OK;
}

HRESULT TE_DCompBuildVisualTree(int count, const TE_IconElementInfo* elements, const HBITMAP* bitmaps, int baseline_y, int overlay_x, int overlay_y)
{
    return TE_DCompBuildVisualTreeForTarget(0, count, elements, bitmaps, baseline_y, overlay_x, overlay_y);
}

static D3DMATRIX MatrixMultiply(const D3DMATRIX& a, const D3DMATRIX& b) {
    D3DMATRIX out;
    __m128 b0 = _mm_loadu_ps(&b.m[0][0]);
    __m128 b1 = _mm_loadu_ps(&b.m[1][0]);
    __m128 b2 = _mm_loadu_ps(&b.m[2][0]);
    __m128 b3 = _mm_loadu_ps(&b.m[3][0]);

    for (int r = 0; r < 4; r++) {
        __m128 a0 = _mm_set1_ps(a.m[r][0]);
        __m128 a1 = _mm_set1_ps(a.m[r][1]);
        __m128 a2 = _mm_set1_ps(a.m[r][2]);
        __m128 a3 = _mm_set1_ps(a.m[r][3]);

        __m128 row = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(a0, b0), _mm_mul_ps(a1, b1)),
            _mm_add_ps(_mm_mul_ps(a2, b2), _mm_mul_ps(a3, b3))
        );
        _mm_storeu_ps(&out.m[r][0], row);
    }
    return out;
}

static D3DMATRIX ComputeTiltPerspectiveMatrix(float tilt_x, float tilt_y, float center_x, float center_y) {
    if (fabsf(tilt_x) < 0.0001f && fabsf(tilt_y) < 0.0001f) {
        D3DMATRIX identity = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        return identity;
    }

    // T1: Translate icon center to origin
    D3DMATRIX t1 = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -center_x, -center_y, 0.0f, 1.0f
    };

    // Rx: Pitch rotation around X axis
    float cos_x = cosf(tilt_x);
    float sin_x = sinf(tilt_x);
    D3DMATRIX rx = {
        1.0f,  0.0f,   0.0f,  0.0f,
        0.0f,  cos_x,  sin_x, 0.0f,
        0.0f, -sin_x,  cos_x, 0.0f,
        0.0f,  0.0f,   0.0f,  1.0f
    };

    // Ry: Yaw rotation around Y axis
    float cos_y = cosf(tilt_y);
    float sin_y = sinf(tilt_y);
    D3DMATRIX ry = {
        cos_y, 0.0f, -sin_y, 0.0f,
        0.0f,  1.0f,  0.0f,  0.0f,
        sin_y, 0.0f,  cos_y, 0.0f,
        0.0f,  0.0f,  0.0f,  1.0f
    };

    // M_persp: Perspective projection with depth d = 1000px
    const float d = 1000.0f;
    D3DMATRIX persp = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, -1.0f / d,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    // T2: Translate origin back to icon center
    D3DMATRIX t2 = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        center_x, center_y, 0.0f, 1.0f
    };

    D3DMATRIX m = MatrixMultiply(t1, rx);
    m = MatrixMultiply(m, ry);
    m = MatrixMultiply(m, persp);
    m = MatrixMultiply(m, t2);
    return m;
}

HRESULT TE_DCompUpdateTransformsForTarget(int target_index, int count, const float* scales,
                                          const float* pos_x, const float* pos_y,
                                          const float* tilt_x, const float* tilt_y)
{
    if (!scales || count <= 0) return TE_E_INVALIDARG;
    if (target_index < 0 || target_index >= TE_MAX_DCOMP_TARGETS) return TE_E_INVALIDARG;
    TE_TargetVisualTree* t = &s_targets[target_index];
    if (!t->is_active) return TE_E_FAIL;
    if (count > t->visual_count) count = t->visual_count;

    for (int i = 0; i < count; i++) {
        if (!t->scale_transforms[i] || !t->translate_transforms[i]) continue;

        t->scale_transforms[i]->SetScaleX(scales[i]);
        t->scale_transforms[i]->SetScaleY(scales[i]);

        if (pos_x) t->translate_transforms[i]->SetOffsetX(pos_x[i]);
        if (pos_y) t->translate_transforms[i]->SetOffsetY(pos_y[i]);

        if (t->matrix_transforms[i]) {
            float tx = tilt_x ? tilt_x[i] : 0.0f;
            float ty = tilt_y ? tilt_y[i] : 0.0f;
            bool is_zero_tilt = (fabsf(tx) < 0.001f && fabsf(ty) < 0.001f);
            if (is_zero_tilt && t->icon_is_identity_matrix[i]) {
                /* PERF-302: Already identity, skip redundant SetMatrix call */
            } else {
                D3DMATRIX mat = ComputeTiltPerspectiveMatrix(tx, ty, t->icon_centers[i].x, t->icon_centers[i].y);
                t->matrix_transforms[i]->SetMatrix(mat);
                t->icon_is_identity_matrix[i] = is_zero_tilt;
            }
        }
    }

    return TE_S_OK;
}

HRESULT TE_DCompUpdateTransforms(int count, const float* scales,
                                  const float* pos_x, const float* pos_y,
                                  const float* tilt_x, const float* tilt_y)
{
    return TE_DCompUpdateTransformsForTarget(0, count, scales, pos_x, pos_y, tilt_x, tilt_y);
}

HRESULT TE_DCompSetOverlayAlphaForTarget(int target_index, float alpha)
{
    if (target_index < 0 || target_index >= TE_MAX_DCOMP_TARGETS) return TE_E_INVALIDARG;
    TE_TargetVisualTree* t = &s_targets[target_index];
    if (!t->is_active) return TE_E_FAIL;

    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    BOOL island_visible = (target_index == 0) ? TE_DynamicIslandIsVisible() : FALSE;

    if (s_custom_start_enabled || island_visible) {
        if (t->root_effect) {
            t->root_effect->SetOpacity(1.0f);
        }
        for (int i = 0; i < t->visual_count; i++) {
            if (t->icon_effect_groups[i]) {
                float op = t->icon_is_start[i] ? 1.0f : alpha;
                t->icon_effect_groups[i]->SetOpacity(op);
            }
        }
    } else {
        if (t->root_effect) {
            t->root_effect->SetOpacity(alpha);
        }
        for (int i = 0; i < t->visual_count; i++) {
            if (t->icon_effect_groups[i]) {
                t->icon_effect_groups[i]->SetOpacity(alpha);
            }
        }
    }

    if (t->overlay_hwnd && IsWindow(t->overlay_hwnd)) {
        if (alpha <= 0.0f && !s_custom_start_enabled && !island_visible) {
            ShowWindow(t->overlay_hwnd, SW_HIDE);
        } else {
            ShowWindow(t->overlay_hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(t->overlay_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }

    return TE_S_OK;
}

HRESULT TE_DCompSetOverlayAlpha(float alpha)
{
    for (int i = 0; i < s_target_count; i++) {
        if (s_targets[i].is_active) {
            TE_DCompSetOverlayAlphaForTarget(i, alpha);
        }
    }

    if (s_dcomp_device) {
        s_dcomp_device->Commit();
    }
    return TE_S_OK;
}

HRESULT TE_DCompHandleDeviceLoss(void)
{
    TE_LogWrite(TE_LOG_WARNING, LOG_TAG, "Handling DirectComposition device loss recovery");

    struct SavedTarget {
        HWND taskbar_hwnd;
        HWND overlay_hwnd;
        bool is_active;
    } saved[TE_MAX_DCOMP_TARGETS] = {};

    int saved_count = s_target_count;
    for (int i = 0; i < s_target_count && i < TE_MAX_DCOMP_TARGETS; i++) {
        saved[i].taskbar_hwnd = s_targets[i].taskbar_hwnd;
        saved[i].overlay_hwnd = s_targets[i].overlay_hwnd;
        saved[i].is_active = s_targets[i].is_active;
    }
    HWND primary_overlay = s_overlay_hwnd_ref ? s_overlay_hwnd_ref : (saved_count > 0 ? saved[0].overlay_hwnd : NULL);

    wchar_t cached_path[MAX_PATH] = {};
    if (s_cached_start_path[0] != L'\0') {
        wcscpy_s(cached_path, MAX_PATH, s_cached_start_path);
    }
    int custom_start = s_custom_start_enabled;

    TE_DCompDestroyDevice();

    if (!primary_overlay || !IsWindow(primary_overlay)) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Device loss recovery aborted: no valid primary overlay window");
        return TE_E_FAIL;
    }

    HRESULT hr = TE_DCompInitDevice(primary_overlay);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Device loss recovery failed to re-initialize device");
        return hr;
    }

    for (int i = 1; i < saved_count; i++) {
        if (saved[i].is_active && saved[i].overlay_hwnd && IsWindow(saved[i].overlay_hwnd)) {
            int out_idx = -1;
            TE_DCompAddTarget(saved[i].taskbar_hwnd, saved[i].overlay_hwnd, &out_idx);
        }
    }

    if (cached_path[0] != L'\0' && custom_start) {
        TE_DCompLoadStartImage(cached_path);
    }

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "DirectComposition device loss recovery succeeded");
    return TE_S_OK;
}

HRESULT TE_DCompCommit(void)
{
    if (!s_dcomp_device) return TE_E_FAIL;

    HRESULT hr = s_dcomp_device->Commit();
    if (FAILED(hr)) {
        HRESULT reason = s_d3d_device ? s_d3d_device->GetDeviceRemovedReason() : S_OK;
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
            hr == D2DERR_RECREATE_TARGET || (s_d3d_device && FAILED(reason))) {
            TE_LogWrite(TE_LOG_WARNING, LOG_TAG, "DComp Commit device loss detected, recovering...");
            return TE_DCompHandleDeviceLoss();
        }
        TE_LogWrite(TE_LOG_WARNING, LOG_TAG, "DComp Commit failed");
        return hr;
    }

    return TE_S_OK;
}

void TE_DCompEnsureTopmost(HWND taskbar_hwnd)
{
    for (int i = 0; i < s_target_count; i++) {
        if (!s_targets[i].is_active || !s_targets[i].overlay_hwnd || !IsWindow(s_targets[i].overlay_hwnd)) continue;
        if (taskbar_hwnd && s_targets[i].taskbar_hwnd && s_targets[i].taskbar_hwnd != taskbar_hwnd) continue;

        HWND overlay = s_targets[i].overlay_hwnd;
        HWND prev = GetWindow(overlay, GW_HWNDPREV);
        if (!prev) continue;

        bool is_taskbar_above = false;
        for (HWND w = prev; w != NULL; w = GetWindow(w, GW_HWNDPREV)) {
            if (taskbar_hwnd && (w == taskbar_hwnd || GetAncestor(w, GA_ROOT) == taskbar_hwnd)) {
                is_taskbar_above = true;
                break;
            }

            WCHAR className[64] = { 0 };
            if (GetClassNameW(w, className, 64) > 0) {
                if (_wcsicmp(className, L"Shell_TrayWnd") == 0 ||
                    _wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0 ||
                    _wcsicmp(className, L"TopLevelWindowForOverflowXamlIsland") == 0) {
                    is_taskbar_above = true;
                    break;
                }
            }
        }

        if (is_taskbar_above) {
            SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

HRESULT TE_DCompLoadStartImage(const wchar_t* image_path)
{
    if (!image_path || !*image_path) return TE_E_INVALIDARG;
    wchar_t resolved_path[MAX_PATH] = {};

    HRESULT hr_com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr_com == S_FALSE) {
        CoUninitialize();
    }

    if (GetFileAttributesW(image_path) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(resolved_path, MAX_PATH, image_path);
    } else {
        wchar_t candidate[MAX_PATH] = {};
        swprintf_s(candidate, MAX_PATH, L"Config\\%ls", image_path);
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(resolved_path, MAX_PATH, candidate);
        } else {
            HMODULE h_mod = NULL;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)TE_DCompLoadStartImage, &h_mod);
            if (h_mod) {
                wchar_t mod_path[MAX_PATH] = {};
                GetModuleFileNameW(h_mod, mod_path, MAX_PATH);
                wchar_t* last_slash = wcsrchr(mod_path, L'\\');
                if (last_slash) *last_slash = L'\0';

                const wchar_t* relatives[] = {
                    L"",
                    L"\\..\\..",
                    L"\\..\\..\\Config",
                    L"\\..\\..\\..",
                    L"\\..\\..\\..\\Config"
                };

                for (const wchar_t* rel : relatives) {
                    swprintf_s(candidate, MAX_PATH, L"%ls%ls\\%ls", mod_path, rel, image_path);
                    wchar_t full_path[MAX_PATH] = {};
                    if (GetFullPathNameW(candidate, MAX_PATH, full_path, NULL)) {
                        if (GetFileAttributesW(full_path) != INVALID_FILE_ATTRIBUTES) {
                            wcscpy_s(resolved_path, MAX_PATH, full_path);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!resolved_path[0] || GetFileAttributesW(resolved_path) == INVALID_FILE_ATTRIBUTES) {
        char err[256];
        snprintf(err, sizeof(err), "Custom start image not found: %ls", image_path);
        TE_LogWrite(TE_LOG_WARNING, LOG_TAG, err);
        return TE_E_FAIL;
    }

    if (s_start_button_bitmap) {
        s_start_button_bitmap->Release();
        s_start_button_bitmap = nullptr;
    }
    if (s_start_pixel_data) {
        free(s_start_pixel_data);
        s_start_pixel_data = nullptr;
        s_start_pixel_w = 0;
        s_start_pixel_h = 0;
        s_start_pixel_stride = 0;
    }

    wcscpy_s(s_cached_start_path, MAX_PATH, resolved_path);

    const wchar_t* ext = PathFindExtensionW(resolved_path);
    bool is_svg = (ext && _wcsicmp(ext, L".svg") == 0);

    if (is_svg) {
        IStream* stream = nullptr;
        HRESULT hr = SHCreateStreamOnFileEx(resolved_path, STGM_READ | STGM_SHARE_DENY_NONE,
                                            FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &stream);
        if (FAILED(hr) || !stream) {
            TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to open SVG stream");
            return TE_E_FAIL;
        }

        if (!s_d3d_device) {
            UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            HRESULT hr_d3d = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                              nullptr, 0, D3D11_SDK_VERSION, &s_d3d_device, nullptr, nullptr);
            if (FAILED(hr_d3d) || !s_d3d_device) {
                D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                  nullptr, 0, D3D11_SDK_VERSION, &s_d3d_device, nullptr, nullptr);
            }
        }
        if (!s_d2d_factory) {
            D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory), (void**)&s_d2d_factory);
        }

        if (s_d3d_device && s_d2d_factory) {
            IDXGIDevice* dxgi = nullptr;
            hr = s_d3d_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi);
            if (SUCCEEDED(hr) && dxgi) {
                ID2D1Factory1* factory1 = nullptr;
                hr = s_d2d_factory->QueryInterface(__uuidof(ID2D1Factory1), (void**)&factory1);
                if (SUCCEEDED(hr) && factory1) {
                    ID2D1Device* d2d_dev = nullptr;
                    hr = factory1->CreateDevice(dxgi, &d2d_dev);
                    if (SUCCEEDED(hr) && d2d_dev) {
                        ID2D1DeviceContext* d2d_dc = nullptr;
                        hr = d2d_dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_dc);
                        if (SUCCEEDED(hr) && d2d_dc) {
                            ID2D1DeviceContext5* dc5 = nullptr;
                            hr = d2d_dc->QueryInterface(__uuidof(ID2D1DeviceContext5), (void**)&dc5);
                            if (SUCCEEDED(hr) && dc5) {
                                ID2D1SvgDocument* svgDoc = nullptr;
                                hr = dc5->CreateSvgDocument(stream, D2D1::SizeF(256.0f, 256.0f), &svgDoc);
                                if (SUCCEEDED(hr) && svgDoc) {
                                    D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(
                                        D2D1_BITMAP_OPTIONS_TARGET,
                                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
                                    );
                                    ID2D1Bitmap1* target_bmp = nullptr;
                                    hr = dc5->CreateBitmap(D2D1::SizeU(256, 256), nullptr, 0, &targetProps, &target_bmp);
                                    if (SUCCEEDED(hr) && target_bmp) {
                                        dc5->SetTarget(target_bmp);
                                        dc5->BeginDraw();
                                        dc5->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
                                        dc5->DrawSvgDocument(svgDoc);
                                        hr = dc5->EndDraw();
                                        dc5->SetTarget(nullptr);
                                        if (SUCCEEDED(hr)) {
                                            s_start_button_bitmap = target_bmp;
                                            s_start_button_bitmap->AddRef();
                                        }
                                        target_bmp->Release();
                                    }
                                    svgDoc->Release();
                                }
                                dc5->Release();
                            }
                            d2d_dc->Release();
                        }
                        d2d_dev->Release();
                    }
                    factory1->Release();
                }
                dxgi->Release();
            }
        }
        stream->Release();

        if (s_start_button_bitmap) {
            s_custom_start_enabled = 1;
            char msg[256];
            snprintf(msg, sizeof(msg), "Successfully loaded SVG start button: %ls", resolved_path);
            TE_LogWrite(TE_LOG_INFO, LOG_TAG, msg);
            return TE_S_OK;
        }
        return TE_E_FAIL;
    }

    /* Raster image loading via WIC (PNG, JPG, BMP, ICO) */
    IWICImagingFactory* pWicFactory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory), (void**)&pWicFactory
    );
    if (FAILED(hr) || !pWicFactory) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create WICImagingFactory");
        return TE_E_FAIL;
    }

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pWicFactory->CreateDecoderFromFilename(
        resolved_path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &pDecoder
    );
    if (FAILED(hr) || !pDecoder) {
        pWicFactory->Release();
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "WIC failed to decode image file");
        return TE_E_FAIL;
    }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr) || !pFrame) {
        pDecoder->Release();
        pWicFactory->Release();
        return TE_E_FAIL;
    }

    IWICFormatConverter* pConverter = nullptr;
    hr = pWicFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr) || !pConverter) {
        pFrame->Release();
        pDecoder->Release();
        pWicFactory->Release();
        return TE_E_FAIL;
    }

    hr = pConverter->Initialize(
        pFrame,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) {
        pConverter->Release();
        pFrame->Release();
        pDecoder->Release();
        pWicFactory->Release();
        return TE_E_FAIL;
    }

    UINT w = 0, h = 0;
    pConverter->GetSize(&w, &h);
    if (w > 0 && h > 0) {
        UINT stride = w * 4;
        UINT buffer_size = stride * h;
        void* pixels = malloc(buffer_size);
        if (pixels) {
            hr = pConverter->CopyPixels(nullptr, stride, buffer_size, (BYTE*)pixels);
            if (SUCCEEDED(hr)) {
                s_start_pixel_data = pixels;
                s_start_pixel_w = w;
                s_start_pixel_h = h;
                s_start_pixel_stride = stride;
            } else {
                free(pixels);
            }
        }
    }

    if (!s_d3d_device) {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr_d3d = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                          nullptr, 0, D3D11_SDK_VERSION, &s_d3d_device, nullptr, nullptr);
        if (FAILED(hr_d3d) || !s_d3d_device) {
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                              nullptr, 0, D3D11_SDK_VERSION, &s_d3d_device, nullptr, nullptr);
        }
    }
    if (!s_d2d_factory) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory), (void**)&s_d2d_factory);
    }

    if (s_d3d_device && s_d2d_factory) {
        IDXGIDevice* dxgi = nullptr;
        hr = s_d3d_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi);
        if (SUCCEEDED(hr) && dxgi) {
            ID2D1Factory1* factory1 = nullptr;
            hr = s_d2d_factory->QueryInterface(__uuidof(ID2D1Factory1), (void**)&factory1);
            if (SUCCEEDED(hr) && factory1) {
                ID2D1Device* d2d_dev = nullptr;
                hr = factory1->CreateDevice(dxgi, &d2d_dev);
                if (SUCCEEDED(hr) && d2d_dev) {
                    ID2D1DeviceContext* d2d_dc = nullptr;
                    hr = d2d_dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_dc);
                    if (SUCCEEDED(hr) && d2d_dc) {
                        d2d_dc->CreateBitmapFromWicBitmap(pConverter, nullptr, &s_start_button_bitmap);
                        d2d_dc->Release();
                    }
                    d2d_dev->Release();
                }
                factory1->Release();
            }
            dxgi->Release();
        }
    }

    pConverter->Release();
    pFrame->Release();
    pDecoder->Release();
    pWicFactory->Release();

    if (s_start_button_bitmap || s_start_pixel_data) {
        s_custom_start_enabled = 1;
        char msg[256];
        snprintf(msg, sizeof(msg), "Successfully loaded custom start button image: %ls (%ux%u)",
                 resolved_path, w, h);
        TE_LogWrite(TE_LOG_INFO, LOG_TAG, msg);
        return TE_S_OK;
    }

    return TE_E_FAIL;
}

void TE_DCompSetCustomStartButtonEnabled(int enabled)
{
    s_custom_start_enabled = enabled;
}

int TE_DCompIsCustomStartButtonEnabled(void)
{
    return s_custom_start_enabled;
}

int TE_DCompGetStartButtonBoundsForTarget(int target_index, RECT* out_rect)
{
    if (!out_rect || !s_custom_start_enabled) return 0;
    if (target_index < 0 || target_index >= TE_MAX_DCOMP_TARGETS) return 0;
    TE_TargetVisualTree* t = &s_targets[target_index];
    if (!t->is_active) return 0;
    if (t->start_button_bounds.right > t->start_button_bounds.left) {
        *out_rect = t->start_button_bounds;
        return 1;
    }
    return 0;
}

int TE_DCompGetStartButtonBounds(RECT* out_rect)
{
    if (!out_rect || !s_custom_start_enabled) return 0;
    for (int i = 0; i < s_target_count; i++) {
        if (s_targets[i].is_active && s_targets[i].start_button_bounds.right > s_targets[i].start_button_bounds.left) {
            *out_rect = s_targets[i].start_button_bounds;
            return 1;
        }
    }
    return 0;
}

HRESULT TE_DCompAttachIslandVisual(IDCompositionVisual* island_visual)
{
    s_island_visual_target0 = island_visual;
    if (s_targets[0].root_visual && island_visual) {
        s_targets[0].root_visual->RemoveVisual(island_visual);
        return s_targets[0].root_visual->AddVisual(island_visual, TRUE, nullptr);
    }
    return TE_S_OK;
}

void TE_DCompDetachIslandVisual(IDCompositionVisual* island_visual)
{
    if (s_targets[0].root_visual && island_visual) {
        s_targets[0].root_visual->RemoveVisual(island_visual);
    }
    if (s_island_visual_target0 == island_visual) {
        s_island_visual_target0 = nullptr;
    }
}

int TE_DCompIsIslandVisible(void)
{
    return TE_DynamicIslandIsVisible() ? 1 : 0;
}
