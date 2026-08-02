#include "dcomp_overlay.h"
#include <dcomp.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <sdk/te_log.h>

using Microsoft::WRL::ComPtr;

static HWND g_overlay_hwnd = NULL;
static ComPtr<ID3D11Device> g_d3dDevice;
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
    if (g_overlay_hwnd) return S_OK;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"TE_IconHoverOverlay";
    RegisterClassW(&wc);

    RECT parent_rect;
    GetWindowRect(parent_hwnd, &parent_rect);
    
    g_overlay_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"",
        WS_POPUP | WS_VISIBLE,
        parent_rect.left, parent_rect.top,
        parent_rect.right - parent_rect.left, parent_rect.bottom - parent_rect.top,
        parent_hwnd, NULL, wc.hInstance, NULL
    );
    if (!g_overlay_hwnd) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        TE_LogWrite(TE_LOG_ERROR, "Overlay CreateWindowExW failed with hr=0x%08X", hr);
        return hr;
    }
    SetLayeredWindowAttributes(g_overlay_hwnd, 0, 255, LWA_ALPHA);

    // D3D11 Init
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &g_d3dDevice, nullptr, nullptr);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "D3D11CreateDevice failed with hr=0x%08X", hr);
        return hr;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = g_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "Query IDXGIDevice failed with hr=0x%08X", hr);
        return hr;
    }

    // DComp Init
    hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&g_dcompDevice));
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "DCompositionCreateDevice failed with hr=0x%08X", hr);
        return hr;
    }

    hr = g_dcompDevice->CreateTargetForHwnd(g_overlay_hwnd, TRUE, &g_dcompTarget);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "CreateTargetForHwnd failed with hr=0x%08X", hr);
        return hr;
    }

    hr = g_dcompDevice->CreateVisual(&g_dcompRoot);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "CreateVisual for root failed with hr=0x%08X", hr);
        return hr;
    }

    g_dcompTarget->SetRoot(g_dcompRoot.Get());

    // Pre-allocate visuals, matrix transforms, and surfaces for all slots
    for (int i = 0; i < TE_MAX_TASKBAR_ICONS; i++) {
        hr = g_dcompDevice->CreateVisual(&g_dcompIcons[i]);
        if (FAILED(hr)) {
            TE_LogWrite(TE_LOG_ERROR, "CreateVisual[%d] failed with hr=0x%08X", i, hr);
            return hr;
        }

        hr = g_dcompDevice->CreateMatrixTransform(&g_dcompTransforms[i]);
        if (FAILED(hr)) {
            TE_LogWrite(TE_LOG_ERROR, "CreateMatrixTransform[%d] failed with hr=0x%08X", i, hr);
            return hr;
        }
        g_dcompIcons[i]->SetTransform(g_dcompTransforms[i].Get());

        hr = g_dcompDevice->CreateSurface(64, 64, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &g_dcompSurfaces[i]);
        if (SUCCEEDED(hr)) {
            g_dcompIcons[i]->SetContent(g_dcompSurfaces[i].Get());
        }

        g_dcompRoot->AddVisual(g_dcompIcons[i].Get(), TRUE, nullptr);
    }

    g_dcompDevice->Commit();
    return S_OK;
}

HRESULT TE_DcompUpdateVisuals(TE_IconHoverState* state)
{
    if (!g_dcompDevice || !state) return E_FAIL;

    // Zero allocations in frame update hot path
    for (int i = 0; i < state->icon_count; i++) {
        float x = state->displaced_x[i];
        float y = state->displaced_y[i];
        float scale = state->scales[i];
        
        D2D1_MATRIX_3X2_F matrix = D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y);
        g_dcompTransforms[i]->SetMatrix(matrix);
    }
    
    g_dcompDevice->Commit();
    return S_OK;
}

void TE_DcompShutdown(void)
{
    if (g_dcompDevice) {
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
    g_d3dDevice.Reset();

    if (g_overlay_hwnd) {
        DestroyWindow(g_overlay_hwnd);
        g_overlay_hwnd = NULL;
    }
    UnregisterClassW(L"TE_IconHoverOverlay", GetModuleHandleW(NULL));
}
