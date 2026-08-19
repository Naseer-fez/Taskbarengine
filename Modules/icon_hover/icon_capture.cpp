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
    if (!app_id) return -1;
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
    if (!hIcon || width <= 0 || height <= 0) return NULL;

    HDC hdc = GetDC(NULL);
    if (!hdc) return NULL;
    HDC memDC = CreateCompatibleDC(hdc);
    if (!memDC) {
        ReleaseDC(NULL, hdc);
        return NULL;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down 32-bit DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm || !bits) {
        if (hbm) DeleteObject(hbm);
        DeleteDC(memDC);
        ReleaseDC(NULL, hdc);
        return NULL;
    }

    memset(bits, 0, width * height * 4);

    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hbm);
    DrawIconEx(memDC, 0, 0, hIcon, width, height, 0, NULL, DI_NORMAL);
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, hdc);

    // If icon does not have per-pixel alpha, set opaque alpha for non-black pixels
    uint32_t* pPixels = (uint32_t*)bits;
    bool has_alpha = false;
    int total_pixels = width * height;
    for (int i = 0; i < total_pixels; i++) {
        if ((pPixels[i] & 0xFF000000) != 0) {
            has_alpha = true;
            break;
        }
    }

    if (!has_alpha) {
        for (int i = 0; i < total_pixels; i++) {
            if ((pPixels[i] & 0x00FFFFFF) != 0) {
                pPixels[i] |= 0xFF000000;
            }
        }
    }

    return hbm;
}

HRESULT TE_IconCaptureGet(const wchar_t* app_id, TE_IconEntry* out_entry)
{
    if (!app_id || !out_entry) return E_POINTER;
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
        hr = SHGetImageList(SHIL_EXTRALARGE, IID_PPV_ARGS(&imageList));
        if (SUCCEEDED(hr) && imageList) {
            SHFILEINFOW sfi = {};
            DWORD_PTR res = SHGetFileInfoW(app_id, FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), 
                                          SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
            if (res != 0) {
                imageList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon);
            }
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
        // Try IShellItemImageFactory for Windows 11 packaged/modern applications
        ComPtr<IShellItemImageFactory> imageFactory;
        if (SUCCEEDED(SHCreateItemFromParsingName(app_id, nullptr, IID_PPV_ARGS(&imageFactory)))) {
            SIZE size = { 256, 256 };
            imageFactory->GetImage(size, SIIGBF_BIGGERSIZEOK | SIIGBF_ICONONLY, &hbm);
        }
    }

    if (!hbm) {
        // Fallback colored fill for unresolvable icons
        HDC hdc = GetDC(NULL);
        if (!hdc) return E_OUTOFMEMORY;
        HDC memDC = CreateCompatibleDC(hdc);
        if (!memDC) {
            ReleaseDC(NULL, hdc);
            return E_OUTOFMEMORY;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 256;
        bmi.bmiHeader.biHeight = -256;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (!hbm || !bits) {
            if (hbm) DeleteObject(hbm);
            DeleteDC(memDC);
            ReleaseDC(NULL, hdc);
            return E_OUTOFMEMORY;
        }

        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hbm);
        int hash = 0;
        for (const wchar_t* p = app_id; *p; p++) hash += *p;
        HBRUSH brush = CreateSolidBrush(RGB((hash * 17) % 256, (hash * 31) % 256, (hash * 47) % 256));
        RECT r = {0, 0, 256, 256};
        FillRect(memDC, &r, brush);
        DeleteObject(brush);
        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
        ReleaseDC(NULL, hdc);

        // Ensure alpha is opaque for fallback tile
        uint32_t* pPixels = (uint32_t*)bits;
        for (int i = 0; i < 256 * 256; i++) {
            pPixels[i] |= 0xFF000000;
        }
    }

    if (!hbm) return E_OUTOFMEMORY;

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
    if (!app_id) return;
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
