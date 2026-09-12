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
    BOOL success = FALSE;
    IUIAutomationCondition* img_cond = NULL;
    VARIANT var_img;
    var_img.vt = VT_I4;
    var_img.lVal = UIA_ImageControlTypeId;
    
    if (SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, var_img, &img_cond)) && img_cond) {
        IUIAutomationElement* img_child = NULL;
        if (SUCCEEDED(btn->FindFirst(TreeScope_Children, img_cond, &img_child)) && img_child) {
            RECT gl_rect = {0, 0, 0, 0};
            if (SUCCEEDED(img_child->get_CurrentBoundingRectangle(&gl_rect))) {
                int w_btn = btn_rect.right - btn_rect.left;
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
            img_child->Release();
        }
        img_cond->Release();
    }
    
    if (!success) {
        int w_btn = btn_rect.right - btn_rect.left;
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
    int w = btn_rect.right - btn_rect.left;
    int h = btn_rect.bottom - btn_rect.top;
    if (w < 16 || w > 200 || h < 16 || h > 200) return TE_ELEM_UNKNOWN;
    
    float aspect = (float)w / (float)h;
    if (aspect < 0.4f || aspect > 2.5f) return TE_ELEM_UNKNOWN;
    
    BSTR name = NULL;
    btn->get_CurrentName(&name);
    
    auto MatchesAny = [&](const wchar_t** patterns, int count) -> BOOL {
        for (int i = 0; i < count; i++) {
            if (ContainsSubstringI(auto_id, patterns[i])) return TRUE;
            if (ContainsSubstringI(class_name, patterns[i])) return TRUE;
            if (ContainsSubstringI(name, patterns[i])) return TRUE;
        }
        return FALSE;
    };
    
    const wchar_t* start_patterns[] = {L"Start", L"StartButton"};
    const wchar_t* search_patterns[] = {L"Search", L"SearchButton", L"SearchHost", L"SearchBox"};
    const wchar_t* system_patterns[] = {L"TaskView", L"TaskViewButton", L"Widgets", L"Weather", L"People", L"InputIndicator"};
    const wchar_t* notify_patterns[] = {L"Notification", L"Notify", L"Clock", L"Tray"};
    
    if (MatchesAny(start_patterns, 2) || MatchesAny(search_patterns, 4) || 
        MatchesAny(system_patterns, 6) || MatchesAny(notify_patterns, 4)) {
        if (name) SysFreeString(name);
        return TE_ELEM_SHELL_CONTROL;
    }
    
    IUIAutomationTreeWalker* walker = NULL;
    if (SUCCEEDED(uia->get_ControlViewWalker(&walker)) && walker) {
        IUIAutomationElement* current = btn;
        current->AddRef();
        for (int i = 0; i < 3; i++) {
            IUIAutomationElement* parent = NULL;
            if (FAILED(walker->GetParentElement(current, &parent)) || !parent) {
                break;
            }
            BSTR parent_class = NULL;
            BSTR parent_id = NULL;
            parent->get_CurrentClassName(&parent_class);
            parent->get_CurrentAutomationId(&parent_id);
            
            BOOL is_tray = FALSE;
            if (ContainsSubstringI(parent_class, L"TrayNotifyWnd") ||
                ContainsSubstringI(parent_class, L"Windows.UI.Composition.DesktopWindowContentBridge") ||
                ContainsSubstringI(parent_id, L"SystemTrayIcon")) {
                is_tray = TRUE;
            }
            
            if (parent_class) SysFreeString(parent_class);
            if (parent_id) SysFreeString(parent_id);
            
            current->Release();
            current = parent;
            
            if (is_tray) {
                current->Release();
                walker->Release();
                if (name) SysFreeString(name);
                return TE_ELEM_SYSTEM_TRAY;
            }
        }
        current->Release();
        walker->Release();
    }
    
    BOOL has_image = FALSE;
    IUIAutomationCondition* img_cond = NULL;
    VARIANT var_img;
    var_img.vt = VT_I4;
    var_img.lVal = UIA_ImageControlTypeId;
    if (SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, var_img, &img_cond)) && img_cond) {
        IUIAutomationElement* img_child = NULL;
        if (SUCCEEDED(btn->FindFirst(TreeScope_Children, img_cond, &img_child)) && img_child) {
            has_image = TRUE;
            img_child->Release();
        }
        img_cond->Release();
    }
    
    BOOL has_app_id = ContainsSubstringI(auto_id, L"AppID:");
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

    HRESULT hr_co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

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
        if (SUCCEEDED(hr_co)) CoUninitialize();
        return TE_E_FAIL;
    }

    /* Get the taskbar UIA element from HWND */
    hr = uia->ElementFromHandle(taskbar_hwnd, &taskbar_elem);
    if (FAILED(hr) || !taskbar_elem) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to get UIA element from taskbar HWND");
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
        taskbar_elem->Release();
        uia->Release();
        if (SUCCEEDED(hr_co)) CoUninitialize();
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

        /* Extract bounding rectangle */
        RECT bounds;
        memset(&bounds, 0, sizeof(bounds));
        hr = btn->get_CurrentBoundingRectangle(&bounds);
        if (FAILED(hr) || (bounds.right - bounds.left) <= 0 || (bounds.bottom - bounds.top) <= 0) {
            btn->Release();
            continue;
        }

        /* Extract automation ID */
        BSTR auto_id = NULL;
        btn->get_CurrentAutomationId(&auto_id);

        BSTR class_name = NULL;
        btn->get_CurrentClassName(&class_name);

        TE_TaskbarElementType type = ClassifyElement(uia, btn, bounds, auto_id, class_name);
        if (type == TE_ELEM_APP_ICON) {
            TE_IconElementInfo* info = &out_cache->items[icon_count];
            info->buttonRect = bounds;
            DiscoverGlyphRect(uia, btn, bounds, &info->glyphRect);
            info->element_type = type;
            CopyAutomationId(info->app_id, 256, auto_id);
            info->icon_index = (int)icon_count; /* Default index; refined by icon_capture */

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
