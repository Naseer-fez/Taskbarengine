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
#include <list>
#include <unordered_map>
#include <string>

/** Maximum capacity for the bounded LRU bitmap cache (SYS-018 & PERF-403). */
#define TE_ICON_CACHE_MAX_ENTRIES 64

/** SHIL_JUMBO provides 256x256 icons (Windows Vista+). */
#ifndef SHIL_JUMBO
#define SHIL_JUMBO 4
#endif

static const char* LOG_TAG = "IconCapture";

/** GUID for IImageList: {46EB5926-582E-4017-9FDF-E899DE541EC5} */
static const IID s_IID_IImageList = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0xDE, 0x54, 0x1E, 0xC5 } };

struct CacheEntry {
    std::wstring key;
    HBITMAP hbmp;
    DWORD pid;
    HWND hwnd;
};

/** Bounded LRU cache (most recently used at head, least recently used at tail). */
static std::list<CacheEntry>* s_lru_list = nullptr;
static std::unordered_map<std::wstring, std::list<CacheEntry>::iterator>* s_lru_map = nullptr;
static SRWLOCK s_cache_lock = SRWLOCK_INIT;

/** System jumbo image list handle, acquired once and reused. */
static IImageList* s_jumbo_list = nullptr;

static bool IsValidLocalFilePath(const wchar_t* path)
{
    if (!path || !*path) return false;

    // Reject UNC / network paths explicitly
    if (path[0] == L'\\' || path[0] == L'/') {
        return false;
    }

    size_t len = wcslen(path);
    if (len >= MAX_PATH) return false;

    // Require standard local drive prefix: e.g. "C:\" or "C:/"
    bool is_drive = (((path[0] >= L'A' && path[0] <= L'Z') ||
                      (path[0] >= L'a' && path[0] <= L'z')) &&
                     path[1] == L':' &&
                     (path[2] == L'\\' || path[2] == L'/'));
    if (!is_drive) {
        return false;
    }

    // Check drive type: must be local fixed, removable, or ramdisk (reject DRIVE_REMOTE / network mapped drives)
    wchar_t root[4] = { path[0], L':', L'\\', L'\0' };
    UINT drive_type = GetDriveTypeW(root);
    if (drive_type != DRIVE_FIXED && drive_type != DRIVE_REMOVABLE && drive_type != DRIVE_RAMDISK) {
        return false;
    }

    // Fast filesystem check: file must exist and not be a directory
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    return true;
}

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

        /* Ensure alpha channel is preserved/populated */
        uint32_t* pixels = (uint32_t*)bits;
        size_t total = (size_t)(width * height);
        BOOL has_alpha = FALSE;
        for (size_t p = 0; p < total; p++) {
            if ((pixels[p] & 0xFF000000) != 0) {
                has_alpha = TRUE;
                break;
            }
        }
        if (!has_alpha) {
            for (size_t p = 0; p < total; p++) {
                if ((pixels[p] & 0x00FFFFFF) != 0) {
                    pixels[p] |= 0xFF000000;
                }
            }
        }
    }

    DeleteDC(hdc_mem);
    ReleaseDC(NULL, hdc_screen);

    return hbmp;
}

static HBITMAP CreateSnapshotBitmap(const RECT* screen_bounds)
{
    if (!screen_bounds) return NULL;
    int w = screen_bounds->right - screen_bounds->left;
    int h = screen_bounds->bottom - screen_bounds->top;
    if (w <= 0 || h <= 0) return NULL;

    HDC hdc_screen = GetDC(NULL);
    HDC hdc_mem = CreateCompatibleDC(hdc_screen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* Top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdc_screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbmp && bits) {
        HGDIOBJ old = SelectObject(hdc_mem, hbmp);
        BitBlt(hdc_mem, 0, 0, w, h, hdc_screen, screen_bounds->left, screen_bounds->top, SRCCOPY);
        SelectObject(hdc_mem, old);

        /* Ensure fully opaque alpha for the captured taskbar button */
        uint32_t* pixels = (uint32_t*)bits;
        size_t total = (size_t)(w * h);
        for (size_t p = 0; p < total; p++) {
            pixels[p] |= 0xFF000000;
        }
    }

    DeleteDC(hdc_mem);
    ReleaseDC(NULL, hdc_screen);
    return hbmp;
}

HRESULT TE_IconCaptureInit(void)
{
    AcquireSRWLockExclusive(&s_cache_lock);
    if (s_lru_list && s_lru_map) {
        ReleaseSRWLockExclusive(&s_cache_lock);
        return TE_S_OK;
    }

    /* Acquire the system jumbo image list */
    if (!s_jumbo_list) {
        HRESULT hr = SHGetImageList(SHIL_JUMBO, s_IID_IImageList, (void**)&s_jumbo_list);
        if (FAILED(hr) || !s_jumbo_list) {
            TE_LogWrite(TE_LOG_WARNING, LOG_TAG,
                        "SHGetImageList(SHIL_JUMBO) failed; falling back to no icon extraction");
            s_jumbo_list = nullptr;
            /* Don't fail init — icon capture is optional for basic magnification */
        }
    }

    s_lru_list = new (std::nothrow) std::list<CacheEntry>();
    s_lru_map = new (std::nothrow) std::unordered_map<std::wstring, std::list<CacheEntry>::iterator>();
    if (!s_lru_list || !s_lru_map) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to allocate icon bitmap cache");
        delete s_lru_list;
        s_lru_list = nullptr;
        delete s_lru_map;
        s_lru_map = nullptr;
        if (s_jumbo_list) {
            s_jumbo_list->Release();
            s_jumbo_list = nullptr;
        }
        ReleaseSRWLockExclusive(&s_cache_lock);
        return TE_E_OUTOFMEMORY;
    }

    ReleaseSRWLockExclusive(&s_cache_lock);
    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "Icon capture subsystem initialized (bounded LRU max=64)");
    return TE_S_OK;
}

void TE_IconCaptureShutdown(void)
{
    AcquireSRWLockExclusive(&s_cache_lock);
    if (s_lru_list) {
        /* Release all cached bitmaps immediately */
        for (auto& entry : *s_lru_list) {
            if (entry.hbmp) {
                DeleteObject(entry.hbmp);
            }
        }
        delete s_lru_list;
        s_lru_list = nullptr;
    }
    if (s_lru_map) {
        delete s_lru_map;
        s_lru_map = nullptr;
    }
    ReleaseSRWLockExclusive(&s_cache_lock);

    if (s_jumbo_list) {
        s_jumbo_list->Release();
        s_jumbo_list = nullptr;
    }

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "Icon capture subsystem shut down");
}

uint32_t TE_IconCaptureGetCacheCount(void)
{
    AcquireSRWLockShared(&s_cache_lock);
    uint32_t count = s_lru_list ? (uint32_t)s_lru_list->size() : 0;
    ReleaseSRWLockShared(&s_cache_lock);
    return count;
}

HRESULT TE_IconCaptureGetBitmapEx(const wchar_t* app_id, int icon_index, const RECT* screen_bounds, HWND hwnd, DWORD pid, HBITMAP* out_bitmap)
{
    if (!out_bitmap) {
        return TE_E_INVALIDARG;
    }

    *out_bitmap = NULL;

    if (hwnd && pid == 0) {
        GetWindowThreadProcessId(hwnd, &pid);
    }

    /* Compute unique cache key */
    std::wstring key = (app_id && *app_id) ? app_id : L"";
    if (key.empty() && screen_bounds) {
        wchar_t buf[64];
        swprintf_s(buf, L"rect_%d_%d", screen_bounds->left, screen_bounds->top);
        key = buf;
    }
    if (key.empty() && icon_index >= 0) {
        wchar_t buf[64];
        swprintf_s(buf, L"idx_%d", icon_index);
        key = buf;
    }
    if (key.empty() && hwnd) {
        wchar_t buf[64];
        swprintf_s(buf, L"hwnd_%p", (void*)hwnd);
        key = buf;
    }
    if (key.empty() && pid > 0) {
        wchar_t buf[64];
        swprintf_s(buf, L"pid_%lu", pid);
        key = buf;
    }
    if (key.empty()) {
        return TE_E_INVALIDARG;
    }

    AcquireSRWLockExclusive(&s_cache_lock);
    if (s_lru_map && s_lru_list) {
        auto it = s_lru_map->find(key);
        if (it != s_lru_map->end() && it->second->hbmp) {
            /* LRU hit: splice entry to head of list */
            s_lru_list->splice(s_lru_list->begin(), *s_lru_list, it->second);
            if (hwnd && !it->second->hwnd) it->second->hwnd = hwnd;
            if (pid != 0 && it->second->pid == 0) it->second->pid = pid;
            *out_bitmap = it->second->hbmp;
            ReleaseSRWLockExclusive(&s_cache_lock);
            return TE_S_OK;
        }
    } else {
        ReleaseSRWLockExclusive(&s_cache_lock);
        return TE_E_FAIL;
    }
    ReleaseSRWLockExclusive(&s_cache_lock);

    HICON hicon = NULL;

    /* Tier 1: Try resolving via SHGetFileInfoW ONLY on valid local file paths (SYS-024) */
    if (app_id && IsValidLocalFilePath(app_id) && s_jumbo_list) {
        SHFILEINFOW sfi = {};
        if (SHGetFileInfoW(app_id, 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX)) {
            s_jumbo_list->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hicon);
        }
    }

    /* Tier 2: Try icon_index with system jumbo list */
    if (!hicon && s_jumbo_list && icon_index >= 0) {
        s_jumbo_list->GetIcon(icon_index, ILD_TRANSPARENT, &hicon);
    }

    HBITMAP hbmp = NULL;
    if (hicon) {
        hbmp = IconToBitmap(hicon, 256, 256);
        DestroyIcon(hicon);
    }

    /* Tier 3: Fallback to screen snapshot */
    if (!hbmp && screen_bounds) {
        hbmp = CreateSnapshotBitmap(screen_bounds);
    }

    if (hbmp) {
        AcquireSRWLockExclusive(&s_cache_lock);
        if (s_lru_map && s_lru_list) {
            auto it2 = s_lru_map->find(key);
            if (it2 != s_lru_map->end()) {
                /* Another thread already inserted this key concurrently */
                DeleteObject(hbmp);
                *out_bitmap = it2->second->hbmp;
                ReleaseSRWLockExclusive(&s_cache_lock);
                return TE_S_OK;
            }

            /* Evict LRU oldest entries if at or above capacity (SYS-018 & PERF-403) */
            while (s_lru_list->size() >= TE_ICON_CACHE_MAX_ENTRIES) {
                CacheEntry& oldest = s_lru_list->back();
                if (oldest.hbmp) {
                    DeleteObject(oldest.hbmp);
                }
                s_lru_map->erase(oldest.key);
                s_lru_list->pop_back();
            }

            s_lru_list->push_front({ key, hbmp, pid, hwnd });
            (*s_lru_map)[key] = s_lru_list->begin();
            *out_bitmap = hbmp;
            ReleaseSRWLockExclusive(&s_cache_lock);
            return TE_S_OK;
        }
        ReleaseSRWLockExclusive(&s_cache_lock);
        DeleteObject(hbmp);
    }

    return TE_E_FAIL;
}

HRESULT TE_IconCaptureGetBitmapWithBounds(const wchar_t* app_id, int icon_index, const RECT* screen_bounds, HBITMAP* out_bitmap)
{
    return TE_IconCaptureGetBitmapEx(app_id, icon_index, screen_bounds, NULL, 0, out_bitmap);
}

HRESULT TE_IconCaptureGetBitmap(const wchar_t* app_id, int icon_index, HBITMAP* out_bitmap)
{
    return TE_IconCaptureGetBitmapWithBounds(app_id, icon_index, NULL, out_bitmap);
}

void TE_IconCaptureInvalidateApp(const wchar_t* app_id)
{
    if (!app_id || !*app_id) return;

    AcquireSRWLockExclusive(&s_cache_lock);
    if (s_lru_map && s_lru_list) {
        for (auto it = s_lru_list->begin(); it != s_lru_list->end(); ) {
            bool matches = (_wcsicmp(it->key.c_str(), app_id) == 0);
            if (!matches) {
                const wchar_t* p = wcsrchr(it->key.c_str(), L'\\');
                if (!p) p = wcsrchr(it->key.c_str(), L'/');
                if (p && _wcsicmp(p + 1, app_id) == 0) {
                    matches = true;
                }
            }

            if (matches) {
                if (it->hbmp) {
                    DeleteObject(it->hbmp);
                }
                s_lru_map->erase(it->key);
                it = s_lru_list->erase(it);
            } else {
                ++it;
            }
        }
    }
    ReleaseSRWLockExclusive(&s_cache_lock);
}

void TE_IconCaptureInvalidateHwnd(HWND hwnd)
{
    if (!hwnd) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    wchar_t exe_path[MAX_PATH] = { 0 };
    wchar_t exe_name[MAX_PATH] = { 0 };
    if (pid != 0) {
        HANDLE h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h_proc) {
            DWORD sz = MAX_PATH;
            if (QueryFullProcessImageNameW(h_proc, 0, exe_path, &sz)) {
                const wchar_t* p = wcsrchr(exe_path, L'\\');
                if (p) wcscpy_s(exe_name, MAX_PATH, p + 1);
            }
            CloseHandle(h_proc);
        }
    }

    AcquireSRWLockExclusive(&s_cache_lock);
    if (s_lru_map && s_lru_list) {
        for (auto it = s_lru_list->begin(); it != s_lru_list->end(); ) {
            bool matches = (it->hwnd == hwnd);
            if (!matches && it->hwnd == NULL) {
                if (pid != 0 && it->pid == pid) matches = true;
                if (exe_path[0] && _wcsicmp(it->key.c_str(), exe_path) == 0) matches = true;
                if (exe_name[0]) {
                    const wchar_t* k_fn = wcsrchr(it->key.c_str(), L'\\');
                    if (!k_fn) k_fn = wcsrchr(it->key.c_str(), L'/');
                    if (k_fn && _wcsicmp(k_fn + 1, exe_name) == 0) matches = true;
                }
            }

            if (matches) {
                if (it->hbmp) {
                    DeleteObject(it->hbmp);
                }
                s_lru_map->erase(it->key);
                it = s_lru_list->erase(it);
            } else {
                ++it;
            }
        }
    }
    ReleaseSRWLockExclusive(&s_cache_lock);
}

void TE_IconCaptureInvalidatePid(DWORD pid)
{
    if (pid == 0) return;

    wchar_t exe_path[MAX_PATH] = { 0 };
    wchar_t exe_name[MAX_PATH] = { 0 };
    HANDLE h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h_proc) {
        DWORD sz = MAX_PATH;
        if (QueryFullProcessImageNameW(h_proc, 0, exe_path, &sz)) {
            const wchar_t* p = wcsrchr(exe_path, L'\\');
            if (p) wcscpy_s(exe_name, MAX_PATH, p + 1);
        }
        CloseHandle(h_proc);
    }

    AcquireSRWLockExclusive(&s_cache_lock);
    if (s_lru_map && s_lru_list) {
        for (auto it = s_lru_list->begin(); it != s_lru_list->end(); ) {
            bool matches = (it->pid == pid);
            if (!matches && exe_path[0] && _wcsicmp(it->key.c_str(), exe_path) == 0) matches = true;
            if (!matches && exe_name[0]) {
                const wchar_t* k_fn = wcsrchr(it->key.c_str(), L'\\');
                if (!k_fn) k_fn = wcsrchr(it->key.c_str(), L'/');
                if (k_fn && _wcsicmp(k_fn + 1, exe_name) == 0) matches = true;
            }

            if (matches) {
                if (it->hbmp) {
                    DeleteObject(it->hbmp);
                }
                s_lru_map->erase(it->key);
                it = s_lru_list->erase(it);
            } else {
                ++it;
            }
        }
    }
    ReleaseSRWLockExclusive(&s_cache_lock);
}

void TE_IconCaptureInvalidate(void)
{
    AcquireSRWLockExclusive(&s_cache_lock);
    if (s_lru_map && s_lru_list) {
        /* Destroy all cached bitmaps immediately */
        for (auto& entry : *s_lru_list) {
            if (entry.hbmp) {
                DeleteObject(entry.hbmp);
            }
        }
        s_lru_list->clear();
        s_lru_map->clear();
    }
    ReleaseSRWLockExclusive(&s_cache_lock);

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "Icon bitmap cache invalidated");
}
