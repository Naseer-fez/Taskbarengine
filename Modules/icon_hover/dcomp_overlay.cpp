#include "dcomp_overlay.h"
#include <windows.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

typedef HRESULT (WINAPI *DCompositionCreateDeviceFunc)(
    IDXGIDevice *dxgiDevice,
    REFIID iid,
    void **dcompositionDevice
);

static HWND g_overlay_hwnd = NULL;
static HMODULE g_dcomp_dll = NULL;
static ComPtr<IDCompositionDevice> g_dcomp_device = nullptr;
static ComPtr<IDCompositionTarget> g_dcomp_target = nullptr;
static ComPtr<IDCompositionVisual> g_root_visual = nullptr;
static std::vector<ComPtr<IDCompositionVisual>> g_icon_visuals;
static std::vector<ComPtr<IDCompositionScaleTransform>> g_scale_transforms;

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

extern "C" HRESULT TE_DCompOverlayCreate(HWND taskbar_hwnd, HMONITOR monitor)
{
    (void)monitor;
    if (g_overlay_hwnd && IsWindow(g_overlay_hwnd)) return S_OK;
    if (!taskbar_hwnd || !IsWindow(taskbar_hwnd)) return E_HANDLE;

    RECT rc;
    GetWindowRect(taskbar_hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HINSTANCE hinst = GetModuleHandleW(NULL);
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = L"TaskbarEngine_DCompOverlay";
    RegisterClassW(&wc);

    g_overlay_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        L"TaskbarEngine_DCompOverlay",
        L"TaskbarEngine Overlay",
        WS_POPUP | WS_VISIBLE,
        rc.left, rc.top, width, height,
        taskbar_hwnd, NULL, hinst, NULL);

    if (!g_overlay_hwnd) return HRESULT_FROM_WIN32(GetLastError());

    SetLayeredWindowAttributes(g_overlay_hwnd, 0, 255, LWA_ALPHA);

    if (!g_dcomp_dll) {
        g_dcomp_dll = LoadLibraryW(L"dcomp.dll");
    }
    if (!g_dcomp_dll) return E_NOTIMPL;

    DCompositionCreateDeviceFunc pfnDCompCreate = (DCompositionCreateDeviceFunc)(void*)GetProcAddress(g_dcomp_dll, "DCompositionCreateDevice");
    if (!pfnDCompCreate) return E_NOTIMPL;

    HRESULT hr = pfnDCompCreate(NULL, __uuidof(IDCompositionDevice), (void**)&g_dcomp_device);
    if (FAILED(hr) || !g_dcomp_device) return hr;

    hr = g_dcomp_device->CreateTargetForHwnd(g_overlay_hwnd, TRUE, &g_dcomp_target);
    if (FAILED(hr) || !g_dcomp_target) return hr;

    hr = g_dcomp_device->CreateVisual(&g_root_visual);
    if (FAILED(hr) || !g_root_visual) return hr;

    g_dcomp_target->SetRoot(g_root_visual.Get());
    g_dcomp_device->Commit();

    return S_OK;
}

extern "C" HRESULT TE_DCompOverlaySetIcons(const TE_TaskbarIcon* icons, const TE_IconEntry* bitmaps, int count)
{
    (void)bitmaps;
    if (!g_dcomp_device || !g_root_visual) return E_UNEXPECTED;

    g_icon_visuals.clear();
    g_scale_transforms.clear();
    g_root_visual->RemoveAllVisuals();

    if (count <= 0 || !icons) {
        g_dcomp_device->Commit();
        return S_OK;
    }

    for (int i = 0; i < count; ++i) {
        ComPtr<IDCompositionVisual> vis;
        if (FAILED(g_dcomp_device->CreateVisual(&vis)) || !vis) continue;

        ComPtr<IDCompositionScaleTransform> scale_tr;
        if (SUCCEEDED(g_dcomp_device->CreateScaleTransform(&scale_tr)) && scale_tr) {
            scale_tr->SetScaleX(1.0f);
            scale_tr->SetScaleY(1.0f);
            vis->SetTransform(scale_tr.Get());
            g_scale_transforms.push_back(scale_tr);
        }

        float center_x = (float)(icons[i].bounds.left + icons[i].bounds.right) / 2.0f;
        float center_y = (float)(icons[i].bounds.top + icons[i].bounds.bottom) / 2.0f;
        vis->SetOffsetX(center_x);
        vis->SetOffsetY(center_y);

        g_root_visual->AddVisual(vis.Get(), FALSE, nullptr);
        g_icon_visuals.push_back(vis);
    }

    g_dcomp_device->Commit();
    return S_OK;
}

extern "C" HRESULT TE_DCompOverlaySetScales(const float scales[], int count)
{
    if (!g_dcomp_device || count <= 0 || !scales) return S_OK;

    int num = (int)g_scale_transforms.size();
    if (count < num) num = count;

    for (int i = 0; i < num; ++i) {
        g_scale_transforms[i]->SetScaleX(scales[i]);
        g_scale_transforms[i]->SetScaleY(scales[i]);
    }
    return S_OK;
}

extern "C" HRESULT TE_DCompOverlayCommit(void)
{
    if (!g_dcomp_device) return S_OK;
    return g_dcomp_device->Commit();
}

extern "C" HRESULT TE_DCompOverlaySetVisible(bool visible)
{
    if (g_overlay_hwnd && IsWindow(g_overlay_hwnd)) {
        ShowWindow(g_overlay_hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    return S_OK;
}

extern "C" void TE_DCompOverlayDestroy(void)
{
    g_scale_transforms.clear();
    g_icon_visuals.clear();
    g_root_visual.Reset();
    g_dcomp_target.Reset();
    g_dcomp_device.Reset();

    if (g_overlay_hwnd && IsWindow(g_overlay_hwnd)) {
        DestroyWindow(g_overlay_hwnd);
        g_overlay_hwnd = NULL;
    }
}
