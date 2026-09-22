#include "taskbar_resize.h"
#include <sdk/te_plugin.h>
#include <sdk/te_events.h>
#include <sdk/te_jsonc.h>
#include <cJSON.h>
#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#define SUBCLASS_RESIZE_ID 0x5442525A /* 'TBRZ' */
#define SUBCLASS_BRIDGE_ID 0x54425242 /* 'TBRB' */

static const PluginContext* g_ctx = NULL;

static void ResizeLog(TE_LogLevel level, const char* fmt, ...) {
    if (g_ctx && g_ctx->log) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        g_ctx->log(level, "TaskbarResize", buf);
    }
}

static TE_ResizeConfig g_config = {
    TE_DEFAULT_TASKBAR_HEIGHT, /* 48px native Windows default */
    0,  /* padding_top adjustment */
    0,  /* padding_bottom adjustment */
    4   /* icon_spacing */
};

static const SettingDescriptor g_settings[] = {
    { "height", "Taskbar Height", "Height of the taskbar in logical pixels (default 48, compact 30-36, ultra 24)", TE_SETTING_INT, { .int_val = { 48, 20, 128, 1 } } },
    { "padding_top", "Top Padding Adjustment", "Fine-tune vertical offset from top in logical pixels", TE_SETTING_INT, { .int_val = { 0, -20, 20, 1 } } },
    { "padding_bottom", "Bottom Padding Adjustment", "Fine-tune vertical offset from bottom in logical pixels", TE_SETTING_INT, { .int_val = { 0, -20, 20, 1 } } },
    { "icon_spacing", "Icon Spacing", "Horizontal spacing between icons in logical pixels", TE_SETTING_INT, { .int_val = { 4, 0, 32, 1 } } }
};

static const PluginSettings g_plugin_settings = {
    g_settings,
    4
};

static const PluginMetadata g_metadata = {
    "taskbar_resize",
    "Taskbar Resize",
    "Dynamically resize the Windows taskbar with pixel-perfect icon centering and multi-DPI work area synchronization.",
    "TaskbarEngine",
    110,
    10, /* low priority to resize before visual overlays */
    TE_API_VERSION
};

static uint32_t g_current_dpi = 96;
static RECT g_original_work_area;
static BOOL g_work_area_saved = FALSE;

static int g_current_child_y_offset = 0;
static int g_current_child_cy = 0;
static HWND g_bridge_hwnd = NULL;

int TE_CalculateCenteringOffset(int new_height, int default_height, int padding_top, int padding_bottom, uint32_t dpi) {
    if (dpi == 0) dpi = 96;
    int new_cy = TE_ScaleDPI(new_height, dpi);
    int default_cy = TE_ScaleDPI(default_height, dpi);
    int base_offset = (new_cy - default_cy) / 2;
    int custom_offset = TE_ScaleDPI(padding_top - padding_bottom, dpi) / 2;
    return base_offset + custom_offset;
}

BOOL TE_CalculateWorkArea(const RECT* monitor_rect, int taskbar_height, uint32_t dpi, RECT* out_work_area) {
    if (!monitor_rect || !out_work_area) return FALSE;
    *out_work_area = *monitor_rect;
    int bar_cy = TE_ScaleDPI(taskbar_height, dpi);
    out_work_area->bottom -= bar_cy;
    return TRUE;
}

static void ApplyWorkArea(int new_height) {
    RECT screen_rect;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &screen_rect, 0)) {
        if (!g_work_area_saved) {
            g_original_work_area = screen_rect;
            g_work_area_saved = TRUE;
        }

        HMONITOR hMon = (g_ctx && g_ctx->monitor) ? g_ctx->monitor : MonitorFromWindow(g_ctx ? g_ctx->taskbar_hwnd : NULL, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(MONITORINFO);
        if (GetMonitorInfoW(hMon, &mi)) {
            RECT target_wa;
            if (new_height == TE_DEFAULT_TASKBAR_HEIGHT) {
                int default_cy = TE_ScaleDPI(TE_DEFAULT_TASKBAR_HEIGHT, g_current_dpi);
                target_wa = mi.rcMonitor;
                target_wa.bottom -= default_cy;
            } else if (!TE_CalculateWorkArea(&mi.rcMonitor, new_height, g_current_dpi, &target_wa)) {
                return;
            }

            RECT cur_wa;
            if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &cur_wa, 0) || !EqualRect(&cur_wa, &target_wa)) {
                SystemParametersInfoW(SPI_SETWORKAREA, 0, &target_wa, SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
                ResizeLog(TE_LOG_INFO, "ApplyWorkArea: new work area bottom=%d", target_wa.bottom);
            }
        }
    }
}

static void RestoreWorkArea(void) {
    RECT target_wa;
    BOOL has_target = FALSE;
    if (g_work_area_saved) {
        target_wa = g_original_work_area;
        has_target = TRUE;
    } else {
        HMONITOR hMon = (g_ctx && g_ctx->monitor) ? g_ctx->monitor : MonitorFromWindow(g_ctx ? g_ctx->taskbar_hwnd : NULL, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(MONITORINFO);
        if (GetMonitorInfoW(hMon, &mi)) {
            int default_cy = TE_ScaleDPI(TE_DEFAULT_TASKBAR_HEIGHT, g_current_dpi);
            target_wa = mi.rcMonitor;
            target_wa.bottom -= default_cy;
            has_target = TRUE;
        }
    }
    if (has_target) {
        RECT cur_wa;
        if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &cur_wa, 0) || !EqualRect(&cur_wa, &target_wa)) {
            SystemParametersInfoW(SPI_SETWORKAREA, 0, &target_wa, SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
            ResizeLog(TE_LOG_INFO, "RestoreWorkArea: restored work area bottom=%d", target_wa.bottom);
        }
    }
    g_work_area_saved = FALSE;
}

static LRESULT CALLBACK BridgeSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    (void)dwRefData;
    switch (msg) {
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, BridgeSubclassProc, uIdSubclass);
            return DefSubclassProc(hwnd, msg, wParam, lParam);
        case WM_WINDOWPOSCHANGING: {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            if (g_current_child_cy > 0 && g_current_child_y_offset != 0) {
                wp->y = g_current_child_y_offset;
                wp->cy = g_current_child_cy;
                wp->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
            }
            break;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void ResizeChildWindows(HWND parent, int new_height) {
    (void)new_height;
    int default_cy = TE_ScaleDPI(TE_DEFAULT_TASKBAR_HEIGHT, g_current_dpi);
    int y_offset = TE_CalculateCenteringOffset(g_config.height, TE_DEFAULT_TASKBAR_HEIGHT,
                                               g_config.padding_top, g_config.padding_bottom,
                                               g_current_dpi);
    int target_child_cy = default_cy;

    g_current_child_y_offset = y_offset;
    g_current_child_cy = target_child_cy;

    RECT pr;
    GetClientRect(parent, &pr);
    int parent_w = pr.right - pr.left;

    HWND child = GetWindow(parent, GW_CHILD);
    while (child) {
        RECT cr;
        if (GetWindowRect(child, &cr)) {
            int cw = cr.right - cr.left;
            POINT pt = { cr.left, cr.top };
            ScreenToClient(parent, &pt);

            WCHAR className[256];
            GetClassNameW(child, className, ARRAYSIZE(className));

            if (wcsstr(className, L"DesktopWindowContentBridge") != NULL) {
                g_bridge_hwnd = child;
                DWORD_PTR ref_data = 0;
                if (!GetWindowSubclass(child, BridgeSubclassProc, SUBCLASS_BRIDGE_ID, &ref_data)) {
                    SetWindowSubclass(child, BridgeSubclassProc, SUBCLASS_BRIDGE_ID, 0);
                }
                SetWindowPos(child, NULL, 0, y_offset, parent_w, target_child_cy,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            } else if (_wcsicmp(className, L"TrayNotifyWnd") == 0 ||
                       _wcsicmp(className, L"ReBarWindow32") == 0 ||
                       _wcsicmp(className, L"Start") == 0) {
                SetWindowPos(child, NULL, pt.x, y_offset, cw, target_child_cy,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static void RestoreChildWindows(HWND parent) {
    int default_cy = TE_ScaleDPI(TE_DEFAULT_TASKBAR_HEIGHT, g_current_dpi);
    g_current_child_y_offset = 0;
    g_current_child_cy = default_cy;

    if (g_bridge_hwnd && IsWindow(g_bridge_hwnd)) {
        RemoveWindowSubclass(g_bridge_hwnd, BridgeSubclassProc, SUBCLASS_BRIDGE_ID);
        g_bridge_hwnd = NULL;
    }

    RECT pr;
    GetClientRect(parent, &pr);
    int parent_w = pr.right - pr.left;

    HWND child = GetWindow(parent, GW_CHILD);
    while (child) {
        RECT cr;
        if (GetWindowRect(child, &cr)) {
            int cw = cr.right - cr.left;
            POINT pt = { cr.left, cr.top };
            ScreenToClient(parent, &pt);

            WCHAR className[256];
            GetClassNameW(child, className, ARRAYSIZE(className));

            if (wcsstr(className, L"DesktopWindowContentBridge") != NULL) {
                RemoveWindowSubclass(child, BridgeSubclassProc, SUBCLASS_BRIDGE_ID);
                SetWindowPos(child, NULL, 0, 0, parent_w, default_cy,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            } else if (_wcsicmp(className, L"TrayNotifyWnd") == 0 ||
                       _wcsicmp(className, L"ReBarWindow32") == 0 ||
                       _wcsicmp(className, L"Start") == 0) {
                SetWindowPos(child, NULL, pt.x, 0, cw, default_cy,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }

    /* Notify parent window to re-layout naturally */
    SendMessageW(parent, WM_SIZE, SIZE_RESTORED, MAKELPARAM(parent_w, default_cy));
    RedrawWindow(parent, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
}

typedef UINT (WINAPI *GetDpiForWindow_t)(HWND);

static void QueryDpi(HWND hwnd) {
    GetDpiForWindow_t pGetDpiForWindow = (GetDpiForWindow_t)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (pGetDpiForWindow && hwnd) {
        UINT dpi = pGetDpiForWindow(hwnd);
        if (dpi > 0) {
            g_current_dpi = dpi;
        }
    }
}

static void ApplySecondaryTaskbars(int height) {
    HWND sec = NULL;
    while ((sec = FindWindowExW(NULL, sec, L"Shell_SecondaryTrayWnd", NULL)) != NULL) {
        UINT sec_dpi = g_current_dpi;
        GetDpiForWindow_t pGetDpiForWindow = (GetDpiForWindow_t)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        if (pGetDpiForWindow) {
            UINT d = pGetDpiForWindow(sec);
            if (d > 0) sec_dpi = d;
        }

        int new_cy = TE_ScaleDPI(height, sec_dpi);
        HMONITOR hMon = MonitorFromWindow(sec, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(MONITORINFO);
        if (GetMonitorInfoW(hMon, &mi)) {
            int x = mi.rcMonitor.left;
            int y = mi.rcMonitor.bottom - new_cy;
            int cx = mi.rcMonitor.right - mi.rcMonitor.left;
            SetWindowPos(sec, NULL, x, y, cx, new_cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            ResizeChildWindows(sec, new_cy);
        }
    }
}

static void RestoreSecondaryTaskbars(void) {
    HWND sec = NULL;
    while ((sec = FindWindowExW(NULL, sec, L"Shell_SecondaryTrayWnd", NULL)) != NULL) {
        UINT sec_dpi = g_current_dpi;
        GetDpiForWindow_t pGetDpiForWindow = (GetDpiForWindow_t)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        if (pGetDpiForWindow) {
            UINT d = pGetDpiForWindow(sec);
            if (d > 0) sec_dpi = d;
        }

        int default_cy = TE_ScaleDPI(TE_DEFAULT_TASKBAR_HEIGHT, sec_dpi);
        HMONITOR hMon = MonitorFromWindow(sec, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(MONITORINFO);
        if (GetMonitorInfoW(hMon, &mi)) {
            int x = mi.rcMonitor.left;
            int y = mi.rcMonitor.bottom - default_cy;
            int cx = mi.rcMonitor.right - mi.rcMonitor.left;
            SetWindowPos(sec, NULL, x, y, cx, default_cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            RestoreChildWindows(sec);
        }
    }
}

static BOOL s_in_layout = FALSE;

static void ApplyTaskbarSize(HWND hwnd) {
    if (!hwnd || s_in_layout) return;
    s_in_layout = TRUE;
    QueryDpi(hwnd);
    int new_cy = TE_ScaleDPI(g_config.height, g_current_dpi);
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfoW(hMon, &mi)) {
        int x = mi.rcMonitor.left;
        int y = mi.rcMonitor.bottom - new_cy;
        int cx = mi.rcMonitor.right - mi.rcMonitor.left;
        ResizeLog(TE_LOG_INFO, "ApplyTaskbarSize: rect=(%d, %d, %d, %d), height=%d, dpi=%u",
                  x, y, cx, new_cy, g_config.height, g_current_dpi);
        SetWindowPos(hwnd, NULL, x, y, cx, new_cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        ApplyWorkArea(g_config.height);
        ResizeChildWindows(hwnd, new_cy);
        ApplySecondaryTaskbars(g_config.height);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
    }
    s_in_layout = FALSE;
}

static void RestoreTaskbarSize(HWND hwnd) {
    if (!hwnd || s_in_layout) return;
    s_in_layout = TRUE;
    QueryDpi(hwnd);
    int default_cy = TE_ScaleDPI(TE_DEFAULT_TASKBAR_HEIGHT, g_current_dpi);
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfoW(hMon, &mi)) {
        int x = mi.rcMonitor.left;
        int y = mi.rcMonitor.bottom - default_cy;
        int cx = mi.rcMonitor.right - mi.rcMonitor.left;
        ResizeLog(TE_LOG_INFO, "RestoreTaskbarSize: rect=(%d, %d, %d, %d)", x, y, cx, default_cy);
        SetWindowPos(hwnd, NULL, x, y, cx, default_cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RestoreWorkArea();
        RestoreChildWindows(hwnd);
        RestoreSecondaryTaskbars();
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
    }
    s_in_layout = FALSE;
}

static LRESULT CALLBACK ResizeSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    (void)dwRefData;
    
    switch (msg) {
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ResizeSubclassProc, uIdSubclass);
            return DefSubclassProc(hwnd, msg, wParam, lParam);
        case WM_GETMINMAXINFO: {
            LRESULT res = DefSubclassProc(hwnd, msg, wParam, lParam);
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            int new_cy = TE_ScaleDPI(g_config.height, g_current_dpi);
            if (mmi->ptMinTrackSize.y > new_cy) {
                mmi->ptMinTrackSize.y = new_cy;
            }
            return res;
        }
        case WM_WINDOWPOSCHANGING: {
            LRESULT res = DefSubclassProc(hwnd, msg, wParam, lParam);
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            int new_cy = TE_ScaleDPI(g_config.height, g_current_dpi);
            HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi;
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(hMon, &mi)) {
                wp->y = mi.rcMonitor.bottom - new_cy;
                wp->cy = new_cy;
                wp->x = mi.rcMonitor.left;
                wp->cx = mi.rcMonitor.right - mi.rcMonitor.left;
                wp->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
            }
            return res;
        }
        case WM_SIZE:
        case WM_WINDOWPOSCHANGED: {
            LRESULT res = DefSubclassProc(hwnd, msg, wParam, lParam);
            if (!s_in_layout) {
                s_in_layout = TRUE;
                int new_cy = TE_ScaleDPI(g_config.height, g_current_dpi);
                ResizeChildWindows(hwnd, new_cy);
                s_in_layout = FALSE;
            }
            return res;
        }
        case WM_DPICHANGED: {
            g_current_dpi = HIWORD(wParam);
            ApplyTaskbarSize(hwnd);
            break;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void ParseConfig(const cJSON* config) {
    if (!config) return;
    const cJSON* height_node = cJSON_GetObjectItemCaseSensitive(config, "height");
    if (cJSON_IsNumber(height_node)) {
        int h = height_node->valueint;
        if (h >= 20 && h <= 128) {
            g_config.height = h;
        }
    }
    const cJSON* pt_node = cJSON_GetObjectItemCaseSensitive(config, "padding_top");
    if (cJSON_IsNumber(pt_node)) {
        g_config.padding_top = pt_node->valueint;
    }
    const cJSON* pb_node = cJSON_GetObjectItemCaseSensitive(config, "padding_bottom");
    if (cJSON_IsNumber(pb_node)) {
        g_config.padding_bottom = pb_node->valueint;
    }
    const cJSON* is_node = cJSON_GetObjectItemCaseSensitive(config, "icon_spacing");
    if (cJSON_IsNumber(is_node)) {
        g_config.icon_spacing = is_node->valueint;
    }
}

static void OnConfigChanged(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    const TE_ConfigChangedData* changed = (const TE_ConfigChangedData*)data;
    if (!changed || !changed->new_config) return;

    const cJSON* enabled_node = cJSON_GetObjectItemCaseSensitive((const cJSON*)changed->new_config, "enabled");
    if (enabled_node && cJSON_IsBool(enabled_node) && !cJSON_IsTrue(enabled_node)) {
        ResizeLog(TE_LOG_INFO, "OnConfigChanged: taskbar_resize disabled via config, restoring default taskbar");
        if (g_ctx && g_ctx->taskbar_hwnd) {
            RestoreTaskbarSize(g_ctx->taskbar_hwnd);
        }
        if (g_ctx && g_ctx->publish_state) {
            StateValue height_val;
            height_val.type = TE_STATE_INT;
            height_val.data.int_val = TE_DEFAULT_TASKBAR_HEIGHT;
            g_ctx->publish_state("taskbar_resize.height", &height_val);
        }
        return;
    }

    ParseConfig((const cJSON*)changed->new_config);
    ResizeLog(TE_LOG_INFO, "OnConfigChanged: new height=%d, padding_top=%d, padding_bottom=%d, icon_spacing=%d",
              g_config.height, g_config.padding_top, g_config.padding_bottom, g_config.icon_spacing);
    ApplyTaskbarSize(g_ctx->taskbar_hwnd);

    /* Re-publish updated height to shared state store */
    if (g_ctx->publish_state) {
        StateValue height_val;
        height_val.type = TE_STATE_INT;
        height_val.data.int_val = g_config.height;
        g_ctx->publish_state("taskbar_resize.height", &height_val);
    }
}

static void OnDpiChanged(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    const TE_DpiChangedData* dpi_data = (const TE_DpiChangedData*)data;
    g_current_dpi = dpi_data->new_dpi;
    ResizeLog(TE_LOG_INFO, "OnDpiChanged: new dpi=%u", g_current_dpi);
    ApplyTaskbarSize(g_ctx->taskbar_hwnd);
}

static HRESULT Initialize(const PluginContext* ctx) {
    g_ctx = ctx;
    g_current_dpi = ctx->dpi ? ctx->dpi : 96;
    ParseConfig(g_ctx->config);
    ResizeLog(TE_LOG_INFO, "Initialize: configured height=%d, dpi=%u", g_config.height, g_current_dpi);
    return TE_S_OK;
}

static HRESULT Enable(void) {
    ResizeLog(TE_LOG_INFO, "Enable: height=%d, applying subclass and resizing", g_config.height);
    SetWindowSubclass(g_ctx->taskbar_hwnd, ResizeSubclassProc, SUBCLASS_RESIZE_ID, 0);
    g_ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged, NULL);
    g_ctx->subscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged, NULL);
    
    ApplyTaskbarSize(g_ctx->taskbar_hwnd);

    /* Publish current height to shared state store for inter-plugin communication */
    if (g_ctx->publish_state) {
        StateValue height_val;
        height_val.type = TE_STATE_INT;
        height_val.data.int_val = g_config.height;
        g_ctx->publish_state("taskbar_resize.height", &height_val);
    }
    
    return TE_S_OK;
}

static HRESULT Disable(void) {
    ResizeLog(TE_LOG_INFO, "Disable: restoring default height");
    if (g_ctx && g_ctx->taskbar_hwnd) {
        RemoveWindowSubclass(g_ctx->taskbar_hwnd, ResizeSubclassProc, SUBCLASS_RESIZE_ID);
        if (g_ctx->unsubscribe) {
            g_ctx->unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged);
            g_ctx->unsubscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged);
        }
        RestoreTaskbarSize(g_ctx->taskbar_hwnd);
    }

    /* Publish default height to shared state store so other plugins sync */
    if (g_ctx && g_ctx->publish_state) {
        StateValue height_val;
        height_val.type = TE_STATE_INT;
        height_val.data.int_val = TE_DEFAULT_TASKBAR_HEIGHT;
        g_ctx->publish_state("taskbar_resize.height", &height_val);
    }
    
    return TE_S_OK;
}

static HRESULT Update(float delta_time) {
    (void)delta_time;
    return TE_S_OK;
}

static HRESULT Shutdown(void) {
    ResizeLog(TE_LOG_INFO, "Shutdown called");
    g_ctx = NULL;
    return TE_S_OK;
}

static const PluginMetadata* GetMetadata(void) {
    return &g_metadata;
}

static const PluginSettings* GetSettings(void) {
    return &g_plugin_settings;
}

static const PluginInterface g_interface = {
    Initialize,
    Enable,
    Disable,
    Update,
    Shutdown,
    GetMetadata,
    GetSettings
};

TE_EXPORT const PluginInterface* GetPluginInterface(void) {
    return &g_interface;
}
