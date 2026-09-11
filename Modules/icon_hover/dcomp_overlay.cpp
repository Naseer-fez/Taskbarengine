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

#include <windows.h>
#include <dcomp.h>
#include <d2d1.h>
#include <stdio.h>
#include <d3d11.h>

#ifdef _MSC_VER
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#endif

static const char* LOG_TAG = "DCompOverlay";
static const wchar_t* OVERLAY_CLASS_NAME = L"TE_IconHoverOverlay";

/** DComp device and visual tree state. */
static ID2D1Factory* s_d2d_factory = nullptr;
static ID3D11Device* s_d3d_device = nullptr;
static IDCompositionDevice* s_dcomp_device = nullptr;
static IDCompositionTarget* s_dcomp_target = nullptr;
static IDCompositionVisual* s_root_visual = nullptr;
static IDCompositionEffectGroup* s_root_effect = nullptr;

/** Per-icon visual array. */
static IDCompositionVisual* s_icon_visuals[TE_HOVER_MAX_ICONS] = {};
static IDCompositionScaleTransform* s_scale_transforms[TE_HOVER_MAX_ICONS] = {};
static IDCompositionTranslateTransform* s_translate_transforms[TE_HOVER_MAX_ICONS] = {};
static IDCompositionSurface* s_icon_surfaces[TE_HOVER_MAX_ICONS] = {};
static int s_visual_count = 0;
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
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS_NAME,
        NULL,
        WS_POPUP | WS_VISIBLE,
        x, y, width, height,
        taskbar_hwnd,
        NULL,
        hinstance,
        NULL
    );

    if (!overlay) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create overlay window");
        return NULL;
    }

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
        DestroyWindow(overlay_hwnd);
        TE_LogWrite(TE_LOG_INFO, LOG_TAG, "Overlay window destroyed");
    }
}

HRESULT TE_DCompInitDevice(HWND overlay_hwnd)
{
    if (!overlay_hwnd) return TE_E_INVALIDARG;

    s_overlay_hwnd_ref = overlay_hwnd;

    /* Create D2D factory */
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), (void**)&s_d2d_factory);
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

    /* Create target bound to overlay window */
    hr = s_dcomp_device->CreateTargetForHwnd(overlay_hwnd, TRUE, &s_dcomp_target);
    if (FAILED(hr) || !s_dcomp_target) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "CreateTargetForHwnd failed");
        s_dcomp_device->Release();
        s_dcomp_device = nullptr;
        return TE_E_FAIL;
    }

    /* Create root visual */
    hr = s_dcomp_device->CreateVisual(&s_root_visual);
    if (FAILED(hr) || !s_root_visual) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create root visual");
        s_dcomp_target->Release();
        s_dcomp_target = nullptr;
        s_dcomp_device->Release();
        s_dcomp_device = nullptr;
        return TE_E_FAIL;
    }

    /* Bind root visual to target */
    hr = s_dcomp_target->SetRoot(s_root_visual);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to set root visual");
        s_root_visual->Release();
        s_root_visual = nullptr;
        s_dcomp_target->Release();
        s_dcomp_target = nullptr;
        s_dcomp_device->Release();
        s_dcomp_device = nullptr;
        return TE_E_FAIL;
    }

    /* Create root effect group for opacity */
    hr = s_dcomp_device->CreateEffectGroup(&s_root_effect);
    if (SUCCEEDED(hr) && s_root_effect) {
        s_root_effect->SetOpacity(0.0f);
        s_root_visual->SetEffect(s_root_effect);
    }
    s_dcomp_device->Commit();

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "DComp device initialized successfully");
    return TE_S_OK;
}

/**
 * Release all per-icon visuals, transforms, and surfaces.
 */
static void ReleaseVisualTree(void)
{
    if (s_root_visual) {
        s_root_visual->RemoveAllVisuals();
    }

    for (int i = 0; i < s_visual_count; i++) {
        if (s_icon_surfaces[i]) { s_icon_surfaces[i]->Release(); s_icon_surfaces[i] = nullptr; }
        if (s_translate_transforms[i]) { s_translate_transforms[i]->Release(); s_translate_transforms[i] = nullptr; }
        if (s_scale_transforms[i]) { s_scale_transforms[i]->Release(); s_scale_transforms[i] = nullptr; }
        if (s_icon_visuals[i]) { s_icon_visuals[i]->Release(); s_icon_visuals[i] = nullptr; }
    }
    s_visual_count = 0;
}

void TE_DCompDestroyDevice(void)
{
    ReleaseVisualTree();

    if (s_root_effect) { s_root_effect->Release(); s_root_effect = nullptr; }
    if (s_root_visual) { s_root_visual->Release(); s_root_visual = nullptr; }
    if (s_dcomp_target) { s_dcomp_target->Release(); s_dcomp_target = nullptr; }
    if (s_dcomp_device) { s_dcomp_device->Release(); s_dcomp_device = nullptr; }
    if (s_d3d_device) { s_d3d_device->Release(); s_d3d_device = nullptr; }
    if (s_d2d_factory) { s_d2d_factory->Release(); s_d2d_factory = nullptr; }

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "DComp device destroyed");
}

HRESULT TE_DCompBuildVisualTree(int count, const RECT* bounds, const HBITMAP* bitmaps)
{
    if (!s_dcomp_device || !s_root_visual) return TE_E_FAIL;
    if (count <= 0 || !bounds) return TE_E_INVALIDARG;
    if (count > TE_HOVER_MAX_ICONS) count = TE_HOVER_MAX_ICONS;

    /* Release any existing visual tree */
    ReleaseVisualTree();

    HRESULT hr;

    for (int i = 0; i < count; i++) {
        /* Create child visual */
        hr = s_dcomp_device->CreateVisual(&s_icon_visuals[i]);
        if (FAILED(hr)) continue;

        /* Create scale transform */
        hr = s_dcomp_device->CreateScaleTransform(&s_scale_transforms[i]);
        if (FAILED(hr)) {
            s_icon_visuals[i]->Release();
            s_icon_visuals[i] = nullptr;
            continue;
        }

        /* Create translate transform */
        hr = s_dcomp_device->CreateTranslateTransform(&s_translate_transforms[i]);
        if (FAILED(hr)) {
            s_scale_transforms[i]->Release();
            s_scale_transforms[i] = nullptr;
            s_icon_visuals[i]->Release();
            s_icon_visuals[i] = nullptr;
            continue;
        }

        /* Set initial transform values (identity) */
        s_scale_transforms[i]->SetScaleX(1.0f);
        s_scale_transforms[i]->SetScaleY(1.0f);
        s_translate_transforms[i]->SetOffsetX(0.0f);
        s_translate_transforms[i]->SetOffsetY(0.0f);

        /* Position the visual at the icon's location */
        float icon_x = (float)bounds[i].left;
        float icon_y = (float)bounds[i].top;
        float icon_w = (float)(bounds[i].right - bounds[i].left);
        float icon_h = (float)(bounds[i].bottom - bounds[i].top);

        /* Set scale center to icon center */
        s_scale_transforms[i]->SetCenterX(icon_w / 2.0f);
        s_scale_transforms[i]->SetCenterY(icon_h / 2.0f);

        /* Set offset to icon position */
        s_icon_visuals[i]->SetOffsetX(icon_x);
        s_icon_visuals[i]->SetOffsetY(icon_y);

        /* Create a DComp surface for the icon bitmap */
        if (bitmaps && bitmaps[i]) {
            int bmp_w = (int)icon_w;
            int bmp_h = (int)icon_h;
            if (bmp_w <= 0) bmp_w = 48;
            if (bmp_h <= 0) bmp_h = 48;

            hr = s_dcomp_device->CreateSurface(
                (UINT)bmp_w, (UINT)bmp_h,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_ALPHA_MODE_PREMULTIPLIED,
                &s_icon_surfaces[i]
            );

            if (SUCCEEDED(hr) && s_icon_surfaces[i]) {
                IDXGISurface* dxgi_surface = nullptr;
                POINT offset_point = {};
                hr = s_icon_surfaces[i]->BeginDraw(NULL, __uuidof(IDXGISurface), (void**)&dxgi_surface, &offset_point);
                if (SUCCEEDED(hr) && dxgi_surface) {
                    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                        D2D1_RENDER_TARGET_TYPE_DEFAULT,
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
                    );
                    ID2D1RenderTarget* rt = nullptr;
                    hr = s_d2d_factory->CreateDxgiSurfaceRenderTarget(dxgi_surface, &props, &rt);
                    
                    if (SUCCEEDED(hr) && rt) {
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
                                rt->BeginDraw();
                                rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
                                D2D1_RECT_F dest_rect = D2D1::RectF(
                                    (float)offset_point.x,
                                    (float)offset_point.y,
                                    (float)(offset_point.x + bmp_w),
                                    (float)(offset_point.y + bmp_h)
                                );
                                rt->DrawBitmap(d2d_bmp, dest_rect);
                                rt->EndDraw();
                                d2d_bmp->Release();
                            }
                        }
                        rt->Release();
                    }
                    dxgi_surface->Release();
                    s_icon_surfaces[i]->EndDraw();
                }

                s_icon_visuals[i]->SetContent(s_icon_surfaces[i]);
            }
        }

        /* Apply transform group: scale then translate */
        IDCompositionTransform* transforms[2] = {
            s_scale_transforms[i],
            s_translate_transforms[i]
        };
        IDCompositionTransform* group = nullptr;
        hr = s_dcomp_device->CreateTransformGroup(transforms, 2, &group);
        if (SUCCEEDED(hr) && group) {
            s_icon_visuals[i]->SetTransform(group);
            group->Release();
        }

        /* Add as child of root */
        if (i == 0) {
            s_root_visual->AddVisual(s_icon_visuals[i], TRUE, nullptr);
        } else {
            s_root_visual->AddVisual(s_icon_visuals[i], TRUE, s_icon_visuals[i - 1]);
        }

        s_visual_count = i + 1;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Built visual tree with %d icon visuals", s_visual_count);
    TE_LogWrite(TE_LOG_INFO, LOG_TAG, msg);

    if (s_dcomp_device) {
        s_dcomp_device->Commit();
    }

    return TE_S_OK;
}

HRESULT TE_DCompUpdateTransforms(int count, const float* scales,
                                  const float* pos_x, const float* pos_y)
{
    if (!scales || count <= 0) return TE_E_INVALIDARG;
    if (count > s_visual_count) count = s_visual_count;

    for (int i = 0; i < count; i++) {
        if (!s_scale_transforms[i] || !s_translate_transforms[i]) continue;

        s_scale_transforms[i]->SetScaleX(scales[i]);
        s_scale_transforms[i]->SetScaleY(scales[i]);

        if (pos_x) s_translate_transforms[i]->SetOffsetX(pos_x[i]);
        if (pos_y) s_translate_transforms[i]->SetOffsetY(pos_y[i]);
    }

    return TE_S_OK;
}


HRESULT TE_DCompSetOverlayAlpha(float alpha)
{
    /* Clamp alpha to [0, 1] */
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    if (s_root_effect) {
        s_root_effect->SetOpacity(alpha);
    }

    if (s_overlay_hwnd_ref && IsWindow(s_overlay_hwnd_ref)) {
        if (alpha <= 0.0f) {
            ShowWindow(s_overlay_hwnd_ref, SW_HIDE);
        } else {
            ShowWindow(s_overlay_hwnd_ref, SW_SHOWNOACTIVATE);
        }
    }

    if (s_dcomp_device) {
        s_dcomp_device->Commit();
    }
    return TE_S_OK;
}

HRESULT TE_DCompCommit(void)
{
    if (!s_dcomp_device) return TE_E_FAIL;

    HRESULT hr = s_dcomp_device->Commit();
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_WARNING, LOG_TAG, "DComp Commit failed");
        return TE_E_FAIL;
    }

    return TE_S_OK;
}
