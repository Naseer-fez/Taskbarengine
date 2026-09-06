/**
 * @file icon_capture.cpp
 * @brief High-resolution icon bitmap extraction and caching for IconHover.
 *
 * Uses SHGetImageList(SHIL_JUMBO) to extract 256x256 icon bitmaps from the
 * system image list. Bitmaps are cached in an unordered_map keyed by app ID
 * to avoid re-querying the shell on each frame.
 */

#include "icon_capture.h"
#include <sdk/te_log.h>

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commoncontrols.h>
#include <shlobj.h>
#include <stdio.h>

#include <unordered_map>
#include <string>

/** SHIL_JUMBO provides 256x256 icons (Windows Vista+). */
#ifndef SHIL_JUMBO
#define SHIL_JUMBO 4
#endif

static const char* LOG_TAG = "IconCapture";

/** Cache of extracted icon bitmaps keyed by app identifier. */
static std::unordered_map<std::wstring, HBITMAP>* s_bitmap_cache = nullptr;

/** System jumbo image list handle, acquired once and reused. */
static IImageList* s_jumbo_list = nullptr;

/**
 * Convert an HICON to an HBITMAP with alpha channel preserved.
 * Creates a 256x256 32-bit DIB section and draws the icon into it.
 */
static HBITMAP IconToBitmap(HICON hicon, int width, int height)
{
    if (!hicon) return NULL;

    HDC hdc_screen = GetDC(NULL);
    HDC hdc_mem = CreateCompatibleDC(hdc_screen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; /* Top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdc_screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbmp && bits) {
        HGDIOBJ old = SelectObject(hdc_mem, hbmp);
        /* Clear to transparent black */
        memset(bits, 0, (size_t)(width * height * 4));
        DrawIconEx(hdc_mem, 0, 0, hicon, width, height, 0, NULL, DI_NORMAL);
        SelectObject(hdc_mem, old);
    }

    DeleteDC(hdc_mem);
    ReleaseDC(NULL, hdc_screen);

    return hbmp;
}

HRESULT TE_IconCaptureInit(void)
{
    if (s_bitmap_cache) {
        /* Already initialized */
        return TE_S_OK;
    }

    /* Acquire the system jumbo image list */
    HRESULT hr = SHGetImageList(SHIL_JUMBO, IID_IImageList, (void**)&s_jumbo_list);
    if (FAILED(hr) || !s_jumbo_list) {
        TE_LogWrite(TE_LOG_WARNING, LOG_TAG,
                    "SHGetImageList(SHIL_JUMBO) failed; falling back to no icon extraction");
        s_jumbo_list = nullptr;
        /* Don't fail init — icon capture is optional for basic magnification */
    }

    s_bitmap_cache = new (std::nothrow) std::unordered_map<std::wstring, HBITMAP>();
    if (!s_bitmap_cache) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to allocate icon bitmap cache");
        if (s_jumbo_list) {
            s_jumbo_list->Release();
            s_jumbo_list = nullptr;
        }
        return TE_E_OUTOFMEMORY;
    }

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "Icon capture subsystem initialized");
    return TE_S_OK;
}

void TE_IconCaptureShutdown(void)
{
    if (s_bitmap_cache) {
        /* Release all cached bitmaps */
        for (auto& pair : *s_bitmap_cache) {
            if (pair.second) {
                DeleteObject(pair.second);
            }
        }
        delete s_bitmap_cache;
        s_bitmap_cache = nullptr;
    }

    if (s_jumbo_list) {
        s_jumbo_list->Release();
        s_jumbo_list = nullptr;
    }

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "Icon capture subsystem shut down");
}

HRESULT TE_IconCaptureGetBitmap(const wchar_t* app_id, int icon_index, HBITMAP* out_bitmap)
{
    if (!app_id || !out_bitmap) {
        return TE_E_INVALIDARG;
    }

    *out_bitmap = NULL;

    if (!s_bitmap_cache) {
        return TE_E_FAIL;
    }

    /* Check cache first */
    std::wstring key(app_id);
    auto it = s_bitmap_cache->find(key);
    if (it != s_bitmap_cache->end() && it->second) {
        *out_bitmap = it->second;
        return TE_S_OK;
    }

    /* Extract from system image list */
    if (!s_jumbo_list) {
        return TE_E_FAIL;
    }

    HICON hicon = NULL;
    HRESULT hr = s_jumbo_list->GetIcon(icon_index, ILD_TRANSPARENT, &hicon);
    if (FAILED(hr) || !hicon) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Failed to extract icon for index %d", icon_index);
        TE_LogWrite(TE_LOG_WARNING, LOG_TAG, msg);
        return TE_E_FAIL;
    }

    /* Convert icon to 256x256 bitmap with alpha */
    HBITMAP hbmp = IconToBitmap(hicon, 256, 256);
    DestroyIcon(hicon);

    if (!hbmp) {
        TE_LogWrite(TE_LOG_WARNING, LOG_TAG, "Failed to convert icon to bitmap");
        return TE_E_FAIL;
    }

    /* Cache the bitmap */
    (*s_bitmap_cache)[key] = hbmp;
    *out_bitmap = hbmp;

    return TE_S_OK;
}

void TE_IconCaptureInvalidate(void)
{
    if (!s_bitmap_cache) return;

    /* Destroy all cached bitmaps */
    for (auto& pair : *s_bitmap_cache) {
        if (pair.second) {
            DeleteObject(pair.second);
        }
    }
    s_bitmap_cache->clear();

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "Icon bitmap cache invalidated");
}
