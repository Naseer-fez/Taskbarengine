#include "dcomp_overlay.h"
#include <dcomp.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <sdk/te_log.h>
#include <sdk/te_debug_trace.h>
#include <vector>

using Microsoft::WRL::ComPtr;

static HWND g_overlay_hwnd = NULL;
static ComPtr<ID3D11Device> g_d3dDevice;
static ComPtr<ID2D1Factory1> g_d2dFactory;
static ComPtr<ID2D1Device> g_d2dDevice;
static ComPtr<ID2D1DeviceContext> g_d2dContext;
static ComPtr<IDCompositionDevice> g_dcompDevice;
static ComPtr<IDCompositionTarget> g_dcompTarget;
static ComPtr<IDCompositionVisual> g_dcompRoot;
static ComPtr<IDCompositionVisual> g_dcompIcons[TE_MAX_TASKBAR_ICONS];
static ComPtr<IDCompositionMatrixTransform> g_dcompTransforms[TE_MAX_TASKBAR_ICONS];
static ComPtr<IDCompositionSurface> g_dcompSurfaces[TE_MAX_TASKBAR_ICONS];

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HRESULT TE_DcompInit(HWND parent_hwnd)
{
    TE_DebugTraceFmt("[TE-DBG] DComp: Init entering parent=0x%p existing_overlay=0x%p\n", (void*)parent_hwnd, (void*)g_overlay_hwnd);
    if (g_overlay_hwnd) return S_OK;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"TE_IconHoverOverlay";
    RegisterClassW(&wc);

    RECT parent_rect;
    GetWindowRect(parent_hwnd, &parent_rect);
    int width = parent_rect.right - parent_rect.left;
    int height = parent_rect.bottom - parent_rect.top;
    if (width <= 0) width = 1920;
    if (height <= 0) height = 48;
    
    TE_DebugTraceFmt("[TE-DBG] DComp: parent rect l=%ld t=%ld r=%ld b=%ld w=%d h=%d\n",
                     parent_rect.left, parent_rect.top, parent_rect.right, parent_rect.bottom, width, height);
    
    g_overlay_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"",
        WS_CHILD | WS_VISIBLE,
        0, 0,
        width, height,
        parent_hwnd, NULL, wc.hInstance, NULL
    );
    TE_DebugTraceFmt("[TE-DBG] DComp: CreateWindowExW overlay=0x%p err=%lu\n", (void*)g_overlay_hwnd, GetLastError());
    if (!g_overlay_hwnd) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        TE_LogWrite(TE_LOG_ERROR, "Overlay CreateWindowExW failed with hr=0x%08X", hr);
        return hr;
    }
    SetLayeredWindowAttributes(g_overlay_hwnd, 0, 255, LWA_ALPHA);

    // D3D11 Init with BGRA support
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &g_d3dDevice, nullptr, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &g_d3dDevice, nullptr, nullptr);
    }
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "D3D11CreateDevice failed with hr=0x%08X", hr);
        TE_DcompShutdown();
        return hr;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = g_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "Query IDXGIDevice failed with hr=0x%08X", hr);
        TE_DcompShutdown();
        return hr;
    }

    // Direct2D Init
    D2D1_FACTORY_OPTIONS d2dOptions = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &d2dOptions, &g_d2dFactory);
    if (SUCCEEDED(hr) && g_d2dFactory) {
        hr = g_d2dFactory->CreateDevice(dxgiDevice.Get(), &g_d2dDevice);
        if (SUCCEEDED(hr) && g_d2dDevice) {
            hr = g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_d2dContext);
        }
    }
    TE_DebugTraceFmt("[TE-DBG] DComp: D2D init returned hr=0x%08X d2dContext=0x%p\n", (unsigned int)hr, g_d2dContext.Get());

    // DComp Init
    hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&g_dcompDevice));
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "DCompositionCreateDevice failed with hr=0x%08X", hr);
        TE_DcompShutdown();
        return hr;
    }

    hr = g_dcompDevice->CreateTargetForHwnd(g_overlay_hwnd, TRUE, &g_dcompTarget);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "CreateTargetForHwnd failed with hr=0x%08X", hr);
        TE_DcompShutdown();
        return hr;
    }

    hr = g_dcompDevice->CreateVisual(&g_dcompRoot);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "CreateVisual for root failed with hr=0x%08X", hr);
        TE_DcompShutdown();
        return hr;
    }

    g_dcompTarget->SetRoot(g_dcompRoot.Get());

    // Pre-allocate visuals, matrix transforms, and surfaces for all slots
    for (int i = 0; i < TE_MAX_TASKBAR_ICONS; i++) {
        hr = g_dcompDevice->CreateVisual(&g_dcompIcons[i]);
        if (FAILED(hr)) {
            TE_DcompShutdown();
            return hr;
        }

        hr = g_dcompDevice->CreateMatrixTransform(&g_dcompTransforms[i]);
        if (FAILED(hr)) {
            TE_DcompShutdown();
            return hr;
        }
        g_dcompIcons[i]->SetTransform(g_dcompTransforms[i].Get());

        hr = g_dcompDevice->CreateSurface(64, 64, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &g_dcompSurfaces[i]);
        if (SUCCEEDED(hr)) {
            g_dcompIcons[i]->SetContent(g_dcompSurfaces[i].Get());
        }

        // Hide initially
        D2D1_MATRIX_3X2_F initialMatrix = D2D1::Matrix3x2F::Scale(0.0f, 0.0f) * D2D1::Matrix3x2F::Translation(-1000.0f, -1000.0f);
        g_dcompTransforms[i]->SetMatrix(initialMatrix);

        g_dcompRoot->AddVisual(g_dcompIcons[i].Get(), TRUE, nullptr);
    }

    g_dcompDevice->Commit();
    TE_DebugTrace("[TE-DBG] DComp: Init complete after Commit\n");
    return S_OK;
}

HRESULT TE_DcompLoadIconSurfaces(TE_IconHoverState* state)
{
    if (!g_dcompDevice || !g_d2dContext || !state) return E_FAIL;
    TE_DebugTraceFmt("[TE-DBG] DComp: LoadIconSurfaces count=%d\n", state->icon_count);

    for (int i = 0; i < state->icon_count && i < TE_MAX_TASKBAR_ICONS; i++) {
        if (!g_dcompSurfaces[i]) continue;

        TE_IconEntry entry = {};
        if (state->icons[i].app_id[0] != L'\0') {
            TE_IconCaptureGet(state->icons[i].app_id, &entry);
        }

        POINT offset = { 0, 0 };
        ComPtr<IDXGISurface> dxgiSurface;
        RECT updateRect = { 0, 0, 64, 64 };
        HRESULT hr = g_dcompSurfaces[i]->BeginDraw(&updateRect, IID_PPV_ARGS(&dxgiSurface), &offset);
        if (SUCCEEDED(hr) && dxgiSurface) {
            D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            ComPtr<ID2D1Bitmap1> targetBitmap;
            hr = g_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bp, &targetBitmap);
            if (SUCCEEDED(hr) && targetBitmap) {
                g_d2dContext->SetTarget(targetBitmap.Get());
                g_d2dContext->BeginDraw();
                g_d2dContext->Clear(D2D1::ColorF(0, 0, 0, 0.0f));

                if (entry.valid && entry.bitmap) {
                    BITMAP bm;
                    if (GetObject(entry.bitmap, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
                        BITMAPINFO bmi = {};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = bm.bmWidth;
                        bmi.bmiHeader.biHeight = -bm.bmHeight; // top-down
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;

                        std::vector<uint32_t> pixels(bm.bmWidth * bm.bmHeight);
                        HDC hdc = GetDC(NULL);
                        GetDIBits(hdc, entry.bitmap, 0, bm.bmHeight, pixels.data(), &bmi, DIB_RGB_COLORS);
                        ReleaseDC(NULL, hdc);

                        D2D1_BITMAP_PROPERTIES bprops = D2D1::BitmapProperties(
                            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                        ComPtr<ID2D1Bitmap> d2dBitmap;
                        if (SUCCEEDED(g_d2dContext->CreateBitmap(D2D1::SizeU(bm.bmWidth, bm.bmHeight), pixels.data(), bm.bmWidth * 4, &bprops, &d2dBitmap))) {
                            g_d2dContext->DrawBitmap(d2dBitmap.Get(), D2D1::RectF((float)offset.x + 8.0f, (float)offset.y + 8.0f, (float)offset.x + 56.0f, (float)offset.y + 56.0f));
                        }
                    }
                } else {
                    // Fallback visual icon tile
                    ComPtr<ID2D1SolidColorBrush> brush;
                    g_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.52f, 0.92f, 0.9f), &brush);
                    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF((float)offset.x + 10.0f, (float)offset.y + 10.0f, (float)offset.x + 54.0f, (float)offset.y + 54.0f), 8.0f, 8.0f);
                    g_d2dContext->FillRoundedRectangle(&rr, brush.Get());
                }

                g_d2dContext->EndDraw();
            }
            g_dcompSurfaces[i]->EndDraw();
        }
    }

    g_dcompDevice->Commit();
    return S_OK;
}

HRESULT TE_DcompUpdateVisuals(TE_IconHoverState* state)
{
    if (!g_dcompDevice || !state || !g_overlay_hwnd) return E_FAIL;

    RECT parent_rect = {};
    GetWindowRect(g_overlay_hwnd, &parent_rect);

    bool any_hover = false;
    for (int i = 0; i < state->icon_count; i++) {
        if (state->scales[i] > 1.005f) {
            any_hover = true;
            break;
        }
    }

    // Zero allocations in frame update hot path
    for (int i = 0; i < state->icon_count; i++) {
        if (!any_hover && !state->was_in_taskbar && !state->settling) {
            // Hide visual when completely idle
            D2D1_MATRIX_3X2_F hiddenMatrix = D2D1::Matrix3x2F::Scale(0.0f, 0.0f) * D2D1::Matrix3x2F::Translation(-1000.0f, -1000.0f);
            g_dcompTransforms[i]->SetMatrix(hiddenMatrix);
            continue;
        }

        float scale = state->scales[i];
        
        // Base center of icon in taskbar overlay coordinates
        float center_x = state->base_centers_x[i] - (float)parent_rect.left;
        float center_y = ((float)state->icons[i].bounds.top + (float)state->icons[i].bounds.bottom) / 2.0f - (float)parent_rect.top;
        
        // Add lateral displacement from magnification curve layout
        float disp_x = state->displaced_x[i] - state->base_centers_x[i];
        
        // Lift icon upward on hover
        float lift_y = (scale - 1.0f) * 16.0f;
        
        // Scale around surface center (32, 32) and translate to target position
        D2D1_MATRIX_3X2_F matrix = 
            D2D1::Matrix3x2F::Translation(-32.0f, -32.0f) *
            D2D1::Matrix3x2F::Scale(scale, scale) *
            D2D1::Matrix3x2F::Translation(center_x + disp_x, center_y - lift_y);
            
        g_dcompTransforms[i]->SetMatrix(matrix);
    }
    
    g_dcompDevice->Commit();
    return S_OK;
}

void TE_DcompShutdown(void)
{
    TE_DebugTraceFmt("[TE-DBG] DComp: Shutdown entering overlay=0x%p\n", (void*)g_overlay_hwnd);
    if (g_dcompDevice && g_dcompTarget) {
        g_dcompTarget->SetRoot(nullptr);
        g_dcompDevice->Commit();
    }
    
    for (int i = 0; i < TE_MAX_TASKBAR_ICONS; i++) {
        g_dcompSurfaces[i].Reset();
        g_dcompTransforms[i].Reset();
        g_dcompIcons[i].Reset();
    }
    g_dcompRoot.Reset();
    g_dcompTarget.Reset();
    g_dcompDevice.Reset();
    g_d2dContext.Reset();
    g_d2dDevice.Reset();
    g_d2dFactory.Reset();
    g_d3dDevice.Reset();

    if (g_overlay_hwnd) {
        DestroyWindow(g_overlay_hwnd);
        g_overlay_hwnd = NULL;
    }
    UnregisterClassW(L"TE_IconHoverOverlay", GetModuleHandleW(NULL));
    TE_DebugTrace("[TE-DBG] DComp: Shutdown complete\n");
}
