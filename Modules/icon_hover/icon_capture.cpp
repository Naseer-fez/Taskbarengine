#include "icon_capture.h"
#include <windows.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

#define MAX_CACHED_ICONS 64

struct InternalIconEntry {
    std::wstring app_id;
    HBITMAP bitmap;
    int width;
    int height;
};

static std::vector<InternalIconEntry> g_cache;

static HBITMAP CreateDefaultIconBitmap(int size)
{
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size; /* Top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc = GetDC(NULL);
    HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (hbmp && bits) {
        /* Fill solid blueish square for placeholder fallback */
        DWORD* pixels = (DWORD*)bits;
        for (int i = 0; i < size * size; ++i) {
            pixels[i] = 0xFF3399FF;
        }
    }
    return hbmp;
}

extern "C" HRESULT TE_IconCaptureInit(void)
{
    g_cache.reserve(MAX_CACHED_ICONS);
    return S_OK;
}

extern "C" HRESULT TE_IconCaptureGet(const wchar_t* app_id, TE_IconEntry* out_entry)
{
    if (!out_entry) return E_POINTER;
    const wchar_t* target_id = (app_id && app_id[0] != L'\0') ? app_id : L"default_app";

    /* Lookup in cache */
    for (const auto& entry : g_cache) {
        if (entry.app_id == target_id) {
            wcsncpy_s(out_entry->app_id, 256, entry.app_id.c_str(), _TRUNCATE);
            out_entry->bitmap = entry.bitmap;
            out_entry->width = entry.width;
            out_entry->height = entry.height;
            return S_OK;
        }
    }

    /* Try extracting icon using SHGetImageList (JUMBO 256x256) */
    HBITMAP extracted_bmp = nullptr;
    int bmp_w = 256;
    int bmp_h = 256;

    ComPtr<IImageList> image_list;
    HRESULT hr = SHGetImageList(SHIL_JUMBO, IID_PPV_ARGS(&image_list));
    if (SUCCEEDED(hr) && image_list) {
        HICON hicon = nullptr;
        if (SUCCEEDED(image_list->GetIcon(0, ILD_TRANSPARENT, &hicon)) && hicon) {
            ICONINFO ii;
            if (GetIconInfo(hicon, &ii)) {
                extracted_bmp = ii.hbmColor;
                if (ii.hbmMask) DeleteObject(ii.hbmMask);
            }
            DestroyIcon(hicon);
        }
    }

    if (!extracted_bmp) {
        extracted_bmp = CreateDefaultIconBitmap(256);
    }

    InternalIconEntry new_entry;
    new_entry.app_id = target_id;
    new_entry.bitmap = extracted_bmp;
    new_entry.width = bmp_w;
    new_entry.height = bmp_h;

    if (g_cache.size() >= MAX_CACHED_ICONS) {
        if (g_cache.front().bitmap) DeleteObject(g_cache.front().bitmap);
        g_cache.erase(g_cache.begin());
    }

    g_cache.push_back(new_entry);

    wcsncpy_s(out_entry->app_id, 256, new_entry.app_id.c_str(), _TRUNCATE);
    out_entry->bitmap = new_entry.bitmap;
    out_entry->width = new_entry.width;
    out_entry->height = new_entry.height;
    return S_OK;
}

extern "C" void TE_IconCaptureInvalidate(const wchar_t* app_id)
{
    if (!app_id) return;
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
        if (it->app_id == app_id) {
            if (it->bitmap) DeleteObject(it->bitmap);
            g_cache.erase(it);
            break;
        }
    }
}

extern "C" void TE_IconCaptureShutdown(void)
{
    for (auto& entry : g_cache) {
        if (entry.bitmap) DeleteObject(entry.bitmap);
    }
    g_cache.clear();
}
