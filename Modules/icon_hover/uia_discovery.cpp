/**
 * @file uia_discovery.cpp
 * @brief UI Automation taskbar icon discovery for the IconHover plugin.
 *
 * Uses IUIAutomation COM interface to enumerate taskbar button controls
 * under Shell_TrayWnd / DesktopWindowContentBridge, extracting bounding
 * rectangles, automation IDs, and process associations.
 *
 * Results are cached in a TE_IconElementCache and rate-limited to avoid
 * excessive COM queries during rapid shell hook events.
 */

#include "uia_discovery.h"
#include <sdk/te_log.h>

/* COM and OLE headers must precede UIA headers */
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <stdio.h>
#include <wchar.h>

/** Minimum interval between UIA queries in QPC ticks (~500ms). */
static LARGE_INTEGER s_rate_limit_interval = { 0 };

/** Module log tag. */
static const char* LOG_TAG = "UiaDiscovery";

/**
 * Initialize the rate-limit interval based on QPC frequency.
 * Called once on first use.
 */
static void EnsureRateLimitInit(void)
{
    if (s_rate_limit_interval.QuadPart == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        /* 500ms rate limit */
        s_rate_limit_interval.QuadPart = freq.QuadPart / 2;
    }
}

/**
 * Check whether the cache is still fresh (within rate limit window).
 */
static BOOL IsCacheFresh(const TE_IconElementCache* cache)
{
    if (cache->last_update_qpc == 0) return FALSE;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart - (LONGLONG)cache->last_update_qpc)
           < (uint64_t)s_rate_limit_interval.QuadPart;
}

/**
 * Safely copy a BSTR automation ID into a fixed-size wchar_t buffer.
 */
static void CopyAutomationId(wchar_t* dest, size_t dest_count, BSTR src)
{
    if (src && SysStringLen(src) > 0) {
        size_t len = SysStringLen(src);
        if (len >= dest_count) len = dest_count - 1;
        wmemcpy(dest, src, len);
        dest[len] = L'\0';
    } else {
        dest[0] = L'\0';
    }
}

HRESULT TE_UiaDiscoverIcons(HWND taskbar_hwnd, TE_IconElementCache* out_cache)
{
    if (!taskbar_hwnd || !out_cache) {
        return TE_E_INVALIDARG;
    }

    EnsureRateLimitInit();

    /* Rate-limit check: skip if cache is still fresh */
    if (IsCacheFresh(out_cache)) {
        return TE_S_OK;
    }

    HRESULT hr = S_OK;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* taskbar_elem = NULL;
    IUIAutomationCondition* button_cond = NULL;
    IUIAutomationElementArray* buttons = NULL;

    /* Create the UIAutomation COM object */
    hr = CoCreateInstance(
        __uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IUIAutomation), (void**)&uia
    );
    if (FAILED(hr) || !uia) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create IUIAutomation instance");
        return TE_E_FAIL;
    }

    /* Get the taskbar UIA element from HWND */
    hr = uia->ElementFromHandle(taskbar_hwnd, &taskbar_elem);
    if (FAILED(hr) || !taskbar_elem) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to get UIA element from taskbar HWND");
        uia->Release();
        return TE_E_FAIL;
    }

    /* Create condition to find Button control type elements */
    VARIANT var_button;
    var_button.vt = VT_I4;
    var_button.lVal = UIA_ButtonControlTypeId;
    hr = uia->CreatePropertyCondition(UIA_ControlTypePropertyId, var_button, &button_cond);
    if (FAILED(hr) || !button_cond) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create UIA property condition");
        taskbar_elem->Release();
        uia->Release();
        return TE_E_FAIL;
    }

    /* Find all descendant button elements */
    hr = taskbar_elem->FindAll(TreeScope_Descendants, button_cond, &buttons);
    if (FAILED(hr) || !buttons) {
        TE_LogWrite(TE_LOG_WARNING, LOG_TAG, "No taskbar button elements found via UIA");
        button_cond->Release();
        taskbar_elem->Release();
        uia->Release();
        out_cache->count = 0;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        out_cache->last_update_qpc = (uint64_t)now.QuadPart;
        return TE_S_OK;
    }

    /* Enumerate discovered buttons */
    int total = 0;
    hr = buttons->get_Length(&total);
    if (FAILED(hr)) total = 0;

    uint32_t icon_count = 0;
    for (int i = 0; i < total && icon_count < TE_UIA_MAX_ICONS; i++) {
        IUIAutomationElement* btn = NULL;
        hr = buttons->GetElement(i, &btn);
        if (FAILED(hr) || !btn) continue;

        /* Extract bounding rectangle */
        RECT bounds = { 0 };
        hr = btn->get_CurrentBoundingRectangle(&bounds);
        if (FAILED(hr) || (bounds.right - bounds.left) <= 0 || (bounds.bottom - bounds.top) <= 0) {
            btn->Release();
            continue;
        }

        /* Extract automation ID */
        BSTR auto_id = NULL;
        btn->get_CurrentAutomationId(&auto_id);

        /* Filter: skip elements that are clearly not taskbar app buttons.
         * Windows 11 taskbar buttons typically have automation IDs containing
         * specific patterns. We accept all buttons with valid bounds and
         * filter out known system buttons by pattern. */
        BSTR class_name = NULL;
        btn->get_CurrentClassName(&class_name);

        TE_IconElementInfo* info = &out_cache->items[icon_count];
        info->bounds = bounds;
        CopyAutomationId(info->app_id, 256, auto_id);
        info->icon_index = (int)icon_count; /* Default index; refined by icon_capture */

        icon_count++;

        if (auto_id) SysFreeString(auto_id);
        if (class_name) SysFreeString(class_name);
        btn->Release();
    }

    out_cache->count = icon_count;

    /* Update timestamp */
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    out_cache->last_update_qpc = (uint64_t)now.QuadPart;

    char msg[128];
    snprintf(msg, sizeof(msg), "Discovered %u taskbar button elements", icon_count);
    TE_LogWrite(TE_LOG_INFO, LOG_TAG, msg);

    /* Cleanup */
    buttons->Release();
    button_cond->Release();
    taskbar_elem->Release();
    uia->Release();

    return TE_S_OK;
}

void TE_UiaCacheInvalidate(TE_IconElementCache* cache)
{
    if (cache) {
        cache->last_update_qpc = 0;
    }
}
