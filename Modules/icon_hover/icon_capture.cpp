#include "icon_capture.h"
#include "icon_hover_internal.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

static TE_IconEntry g_cache[TE_MAX_TASKBAR_ICONS] = {};

HRESULT TE_IconCaptureInit(void)
{
    return S_OK;
}

static int FindCacheSlot(const wchar_t* app_id)
{
    for (int i = 0; i < TE_MAX_TASKBAR_ICONS; i++) {
        if (g_cache[i].valid && wcscmp(g_cache[i].app_id, app_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int FindFreeSlot(void)
{
    for (int i = 0; i < TE_MAX_TASKBAR_ICONS; i++) {
        if (!g_cache[i].valid) return i;
    }
    return -1;
}

static HBITMAP CreateBitmapFromIcon(HICON hIcon, int width, int height)
{
    if (!hIcon) return NULL;

    HDC hdc = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP hbm = CreateCompatibleBitmap(hdc, width, height);
    
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hbm);
    
    RECT r = {0, 0, width, height};
    HBRUSH transparentBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(memDC, &r, transparentBrush);

    DrawIconEx(memDC, 0, 0, hIcon, width, height, 0, NULL, DI_NORMAL);

    SelectObject(memDC, oldBmp); // Restore original bitmap before deletion (Fix F15)
    DeleteDC(memDC);
    ReleaseDC(NULL, hdc);

    return hbm;
}

HRESULT TE_IconCaptureGet(const wchar_t* app_id, TE_IconEntry* out_entry)
{
    if (!out_entry) return E_POINTER;
    out_entry->valid = false;

    int slot = FindCacheSlot(app_id);
    if (slot >= 0) {
        *out_entry = g_cache[slot];
        return S_OK;
    }

    slot = FindFreeSlot();
    if (slot < 0) return E_OUTOFMEMORY;

    HICON hIcon = NULL;
    ComPtr<IImageList> imageList;
    HRESULT hr = SHGetImageList(SHIL_JUMBO, IID_PPV_ARGS(&imageList));
    if (SUCCEEDED(hr) && imageList) {
        SHFILEINFOW sfi = {};
        DWORD_PTR res = SHGetFileInfoW(app_id, FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), 
                                      SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
        if (res != 0) {
            imageList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon);
        }
    }

    if (!hIcon) {
        // Fallback to ExtractIconW
        hIcon = ExtractIconW(GetModuleHandleW(NULL), app_id, 0);
    }

    HBITMAP hbm = NULL;
    if (hIcon) {
        hbm = CreateBitmapFromIcon(hIcon, 256, 256);
        DestroyIcon(hIcon);
    } else {
        // Fallback colored fill for unresolvable icons
        HDC hdc = GetDC(NULL);
        HDC memDC = CreateCompatibleDC(hdc);
        hbm = CreateCompatibleBitmap(hdc, 256, 256);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hbm);
        
        int hash = 0;
        for (const wchar_t* p = app_id; *p; p++) hash += *p;
        HBRUSH brush = CreateSolidBrush(RGB((hash*17)%256, (hash*31)%256, (hash*47)%256));
        RECT r = {0, 0, 256, 256};
        FillRect(memDC, &r, brush);
        DeleteObject(brush);
        
        SelectObject(memDC, oldBmp); // Restore original bitmap before deletion (Fix F15)
        DeleteDC(memDC);
        ReleaseDC(NULL, hdc);
    }

    g_cache[slot].bitmap = hbm;
    g_cache[slot].width = 256;
    g_cache[slot].height = 256;
    wcsncpy(g_cache[slot].app_id, app_id, 255);
    g_cache[slot].app_id[255] = L'\0';
    g_cache[slot].valid = true;

    *out_entry = g_cache[slot];
    return S_OK;
}

void TE_IconCaptureInvalidate(const wchar_t* app_id)
{
    int slot = FindCacheSlot(app_id);
    if (slot >= 0) {
        if (g_cache[slot].bitmap) DeleteObject(g_cache[slot].bitmap);
        g_cache[slot].valid = false;
    }
}

void TE_IconCaptureShutdown(void)
{
    for (int i = 0; i < TE_MAX_TASKBAR_ICONS; i++) {
        if (g_cache[i].valid && g_cache[i].bitmap) {
            DeleteObject(g_cache[i].bitmap);
            g_cache[i].valid = false;
        }
    }
}
