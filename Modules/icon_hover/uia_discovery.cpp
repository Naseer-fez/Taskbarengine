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
#include <math.h>

/** Minimum interval between UIA queries in QPC ticks (~500ms). */
static LARGE_INTEGER s_rate_limit_interval = {};

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

static BOOL ContainsSubstringI(BSTR str, const wchar_t* sub) {
    if (!str || !sub) return FALSE;
    size_t len = wcslen(str);
    size_t sub_len = wcslen(sub);
    if (sub_len > len) return FALSE;
    for (size_t i = 0; i <= len - sub_len; i++) {
        if (_wcsnicmp(str + i, sub, sub_len) == 0) return TRUE;
    }
    return FALSE;
}

static BOOL DiscoverGlyphRect(IUIAutomation* uia, IUIAutomationElement* btn, RECT btn_rect, RECT* out_glyph) {
    (void)uia;
    BOOL success = FALSE;
    int w_btn = btn_rect.right - btn_rect.left;

    /* Check pre-cached child elements (0 RPC roundtrips) */
    IUIAutomationElementArray* children = NULL;
    if (btn && SUCCEEDED(btn->GetCachedChildren(&children)) && children) {
        int child_count = 0;
        children->get_Length(&child_count);
        for (int c = 0; c < child_count; c++) {
            IUIAutomationElement* img_child = NULL;
            if (SUCCEEDED(children->GetElement(c, &img_child)) && img_child) {
                CONTROLTYPEID ctype = 0;
                img_child->get_CachedControlType(&ctype);
                if (ctype == UIA_ImageControlTypeId) {
                    RECT gl_rect = { 0 };
                    if (SUCCEEDED(img_child->get_CachedBoundingRectangle(&gl_rect))) {
                        int w_gl = gl_rect.right - gl_rect.left;
                        int h_gl = gl_rect.bottom - gl_rect.top;

                        if (w_gl >= 8 && h_gl >= 8 &&
                            gl_rect.left >= btn_rect.left - 2 &&
                            gl_rect.top >= btn_rect.top - 2 &&
                            gl_rect.right <= btn_rect.right + 2 &&
                            gl_rect.bottom <= btn_rect.bottom + 2) {

                            float aspect = (float)w_gl / (float)h_gl;
                            float size_ratio = (float)w_gl / (float)w_btn;

                            if (aspect >= 0.6f && aspect <= 1.4f &&
                                size_ratio >= 0.35f && size_ratio <= 0.90f) {
                                *out_glyph = gl_rect;
                                success = TRUE;
                            }
                        }
                    }
                }
                img_child->Release();
            }
            if (success) break;
        }
        children->Release();
    }

    /* Fallback to standard 60% proportional taskbar glyph geometry */
    if (!success) {
        int h_btn = btn_rect.bottom - btn_rect.top;
        int targetGlyphDim = (int)(h_btn * 0.60f + 0.5f);
        int insetX = (w_btn - targetGlyphDim) / 2;
        int insetY = (h_btn - targetGlyphDim) / 2;
        out_glyph->left = btn_rect.left + insetX;
        out_glyph->top = btn_rect.top + insetY;
        out_glyph->right = out_glyph->left + targetGlyphDim;
        out_glyph->bottom = out_glyph->top + targetGlyphDim;
    }
    return TRUE;
}

static TE_TaskbarElementType ClassifyElement(IUIAutomation* uia, IUIAutomationElement* btn, RECT btn_rect, BSTR auto_id, BSTR class_name) {
    (void)uia;
    int w = btn_rect.right - btn_rect.left;
    int h = btn_rect.bottom - btn_rect.top;
    if (w < 16 || w > 200 || h < 16 || h > 200) return TE_ELEM_UNKNOWN;

    float aspect = (float)w / (float)h;
    if (aspect < 0.4f || aspect > 2.5f) return TE_ELEM_UNKNOWN;

    BSTR name = NULL;
    if (btn && FAILED(btn->get_CachedName(&name)) || !name) {
        if (btn) btn->get_CurrentName(&name);
    }

    auto MatchesAny = [&](const wchar_t** patterns, int count) -> BOOL {
        for (int i = 0; i < count; i++) {
            if (ContainsSubstringI(auto_id, patterns[i])) return TRUE;
            if (ContainsSubstringI(class_name, patterns[i])) return TRUE;
            if (ContainsSubstringI(name, patterns[i])) return TRUE;
        }
        return FALSE;
    };

    const wchar_t* start_patterns[] = { L"Start", L"StartButton" };
    const wchar_t* search_patterns[] = { L"Search", L"SearchButton", L"SearchHost", L"SearchBox" };
    const wchar_t* system_patterns[] = { L"TaskView", L"TaskViewButton", L"Widgets", L"Weather", L"People", L"InputIndicator", L"Copilot" };
    const wchar_t* notify_patterns[] = {
        L"Notification", L"Notify", L"Clock", L"Tray", L"Overflow",
        L"Chevron", L"QuickSettings", L"ControlCenter", L"SystemTrayIcon",
        L"TrayNotifyWnd"
    };

    if (MatchesAny(start_patterns, 2)) {
        if (name) SysFreeString(name);
        return TE_ELEM_START_BUTTON;
    }

    if (MatchesAny(search_patterns, 4) || MatchesAny(system_patterns, 7)) {
        if (name) SysFreeString(name);
        return TE_ELEM_SHELL_CONTROL;
    }

    if (MatchesAny(notify_patterns, 10)) {
        if (name) SysFreeString(name);
        return TE_ELEM_SYSTEM_TRAY;
    }

    /* Fast cached check for child image elements (0 RPC roundtrips) */
    BOOL has_image = FALSE;
    IUIAutomationElementArray* children = NULL;
    if (btn && SUCCEEDED(btn->GetCachedChildren(&children)) && children) {
        int child_count = 0;
        children->get_Length(&child_count);
        for (int c = 0; c < child_count; c++) {
            IUIAutomationElement* ch = NULL;
            if (SUCCEEDED(children->GetElement(c, &ch)) && ch) {
                CONTROLTYPEID ct = 0;
                ch->get_CachedControlType(&ct);
                if (ct == UIA_ImageControlTypeId) {
                    has_image = TRUE;
                }
                ch->Release();
            }
            if (has_image) break;
        }
        children->Release();
    }

    /* Fast cached check for invoke pattern */
    if (!has_image && btn) {
        IUnknown* unk = NULL;
        if (SUCCEEDED(btn->GetCachedPattern(UIA_InvokePatternId, &unk)) && unk) {
            has_image = TRUE;
            unk->Release();
        }
    }

    BOOL has_app_id = ContainsSubstringI(auto_id, L"AppID:") ||
                      ContainsSubstringI(auto_id, L"Taskbar") ||
                      ContainsSubstringI(auto_id, L"App");
    if (name) SysFreeString(name);

    if (!has_image && !has_app_id) {
        return TE_ELEM_UNKNOWN;
    }

    return TE_ELEM_APP_ICON;
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

    /* Enforce MTA strictly across all UIA discovery routines (SYS-007 & PERF-404) */
    HRESULT hr_co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr_co == RPC_E_CHANGED_MODE) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Apartment mode collision: thread is not MTA (RPC_E_CHANGED_MODE)");
        return RPC_E_CHANGED_MODE;
    }
    if (FAILED(hr_co)) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to initialize COM MTA on discovery thread");
        return hr_co;
    }

    HRESULT hr = S_OK;
    IUIAutomation* uia = NULL;
    IUIAutomationElement* taskbar_elem = NULL;
    IUIAutomationCondition* button_cond = NULL;
    IUIAutomationElementArray* buttons = NULL;
    IUIAutomationCacheRequest* cache_req = NULL;

    /* Create the UIAutomation COM object */
    hr = CoCreateInstance(
        __uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IUIAutomation), (void**)&uia
    );
    if (FAILED(hr) || !uia) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create IUIAutomation instance");
        if (SUCCEEDED(hr_co)) CoUninitialize();
        return TE_E_FAIL;
    }

    /* Batch-fetch properties and patterns in a single cross-process RPC roundtrip (SYS-006 & PERF-401) */
    hr = uia->CreateCacheRequest(&cache_req);
    if (SUCCEEDED(hr) && cache_req) {
        cache_req->AddProperty(UIA_BoundingRectanglePropertyId);
        cache_req->AddProperty(UIA_AutomationIdPropertyId);
        cache_req->AddProperty(UIA_ClassNamePropertyId);
        cache_req->AddProperty(UIA_NamePropertyId);
        cache_req->AddProperty(UIA_ControlTypePropertyId);
        cache_req->AddProperty(UIA_NativeWindowHandlePropertyId);
        cache_req->AddProperty(UIA_ProcessIdPropertyId);
        cache_req->AddPattern(UIA_InvokePatternId);
        cache_req->put_TreeScope((TreeScope)(TreeScope_Element | TreeScope_Children));
    }

    /* Get the taskbar UIA element from HWND */
    hr = uia->ElementFromHandle(taskbar_hwnd, &taskbar_elem);
    if (FAILED(hr) || !taskbar_elem) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to get UIA element from taskbar HWND");
        if (cache_req) cache_req->Release();
        uia->Release();
        if (SUCCEEDED(hr_co)) CoUninitialize();
        return TE_E_FAIL;
    }

    /* Create condition to find Button control type elements */
    VARIANT var_button;
    var_button.vt = VT_I4;
    var_button.lVal = UIA_ButtonControlTypeId;
    hr = uia->CreatePropertyCondition(UIA_ControlTypePropertyId, var_button, &button_cond);
    if (FAILED(hr) || !button_cond) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create UIA property condition");
        if (cache_req) cache_req->Release();
        taskbar_elem->Release();
        uia->Release();
        if (SUCCEEDED(hr_co)) CoUninitialize();
        return TE_E_FAIL;
    }

    /* Find all descendant button elements with pre-cached properties */
    if (cache_req) {
        hr = taskbar_elem->FindAllBuildCache(TreeScope_Descendants, button_cond, cache_req, &buttons);
    } else {
        hr = taskbar_elem->FindAll(TreeScope_Descendants, button_cond, &buttons);
    }
    if (FAILED(hr) || !buttons) {
        TE_LogWrite(TE_LOG_WARNING, LOG_TAG, "No taskbar button elements found via UIA");
        if (cache_req) cache_req->Release();
        button_cond->Release();
        taskbar_elem->Release();
        uia->Release();
        out_cache->count = 0;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        out_cache->last_update_qpc = (uint64_t)now.QuadPart;
        if (SUCCEEDED(hr_co)) CoUninitialize();
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

        /* Extract bounding rectangle from cache (0 RPC) or fallback */
        RECT bounds;
        memset(&bounds, 0, sizeof(bounds));
        if (FAILED(btn->get_CachedBoundingRectangle(&bounds)) || (bounds.right - bounds.left) <= 0 || (bounds.bottom - bounds.top) <= 0) {
            btn->get_CurrentBoundingRectangle(&bounds);
        }
        if ((bounds.right - bounds.left) <= 0 || (bounds.bottom - bounds.top) <= 0) {
            btn->Release();
            continue;
        }

        /* Extract automation ID from cache or fallback */
        BSTR auto_id = NULL;
        if (FAILED(btn->get_CachedAutomationId(&auto_id)) || !auto_id) {
            btn->get_CurrentAutomationId(&auto_id);
        }

        /* Extract class name from cache or fallback */
        BSTR class_name = NULL;
        if (FAILED(btn->get_CachedClassName(&class_name)) || !class_name) {
            btn->get_CurrentClassName(&class_name);
        }

        TE_TaskbarElementType type = ClassifyElement(uia, btn, bounds, auto_id, class_name);
        if (type == TE_ELEM_APP_ICON || type == TE_ELEM_START_BUTTON) {
            TE_IconElementInfo* info = &out_cache->items[icon_count];
            info->buttonRect = bounds;
            DiscoverGlyphRect(uia, btn, bounds, &info->glyphRect);
            info->element_type = type;
            CopyAutomationId(info->app_id, 256, auto_id);
            info->icon_index = (int)icon_count; /* Default index; refined by icon_capture */

            /* Extract HWND and PID from pre-cached properties (SYS-018 & PERF-403) */
            UIA_HWND uia_hwnd = NULL;
            if (FAILED(btn->get_CachedNativeWindowHandle(&uia_hwnd)) || !uia_hwnd) {
                btn->get_CurrentNativeWindowHandle(&uia_hwnd);
            }
            info->hwnd = (HWND)uia_hwnd;

            int uia_pid = 0;
            if (FAILED(btn->get_CachedProcessId(&uia_pid)) || uia_pid <= 0) {
                btn->get_CurrentProcessId(&uia_pid);
            }
            info->pid = (uia_pid > 0) ? (DWORD)uia_pid : 0;

            if (info->pid == 0 && info->hwnd && IsWindow(info->hwnd)) {
                GetWindowThreadProcessId(info->hwnd, &info->pid);
            }

            /* If app_id string is empty but we have a PID, resolve process image path */
            if (info->app_id[0] == L'\0' && info->pid > 0) {
                HANDLE h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info->pid);
                if (h_proc) {
                    DWORD sz = 256;
                    QueryFullProcessImageNameW(h_proc, 0, info->app_id, &sz);
                    CloseHandle(h_proc);
                }
            }

            icon_count++;
        }

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
    if (cache_req) cache_req->Release();
    buttons->Release();
    button_cond->Release();
    taskbar_elem->Release();
    uia->Release();

    if (SUCCEEDED(hr_co)) CoUninitialize();

    return TE_S_OK;
}

void TE_UiaCacheInvalidate(TE_IconElementCache* cache)
{
    if (cache) {
        cache->last_update_qpc = 0;
    }
}

HRESULT TE_UiaHideStartButton(HWND taskbar_hwnd, BOOL hide)
{
    if (!taskbar_hwnd) return TE_E_INVALIDARG;

    HRESULT result = TE_S_FALSE;

    /* 1. Check direct child Start button HWND on primary taskbar */
    HWND start_hwnd = FindWindowExW(taskbar_hwnd, NULL, L"Start", NULL);
    if (start_hwnd) {
        ShowWindow(start_hwnd, hide ? SW_HIDE : SW_SHOW);
        result = TE_S_OK;
    }

    /* 2. Check child Start button in secondary taskbars if present */
    HWND sec_tray = FindWindowW(L"Shell_SecondaryTrayWnd", NULL);
    while (sec_tray) {
        HWND sec_start = FindWindowExW(sec_tray, NULL, L"Start", NULL);
        if (sec_start) {
            ShowWindow(sec_start, hide ? SW_HIDE : SW_SHOW);
            result = TE_S_OK;
        }
        sec_tray = FindWindowExW(NULL, sec_tray, L"Shell_SecondaryTrayWnd", NULL);
    }

    /* 3. Search children of taskbar for windows with Start class or window text */
    EnumChildWindows(taskbar_hwnd, [](HWND child, LPARAM lParam) -> BOOL {
        wchar_t cls[64] = {};
        wchar_t text[64] = {};
        GetClassNameW(child, cls, 64);
        GetWindowTextW(child, text, 64);
        if (_wcsicmp(cls, L"Start") == 0 || _wcsicmp(text, L"Start") == 0) {
            BOOL should_hide = (BOOL)lParam;
            ShowWindow(child, should_hide ? SW_HIDE : SW_SHOW);
        }
        return TRUE;
    }, (LPARAM)hide);

    return result;
}

HRESULT TE_UiaHideStartButtonAll(BOOL hide)
{
    HWND primary = FindWindowW(L"Shell_TrayWnd", NULL);
    if (primary) {
        TE_UiaHideStartButton(primary, hide);
    }
    HWND sec = NULL;
    while ((sec = FindWindowExW(NULL, sec, L"Shell_SecondaryTrayWnd", NULL)) != NULL) {
        TE_UiaHideStartButton(sec, hide);
    }
    return TE_S_OK;
}
