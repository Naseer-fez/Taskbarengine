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

static struct {
    int height;
} g_config = { 24 }; /* 24px = 50% smaller than standard 48px */

static const SettingDescriptor g_settings[] = {
    { "height", "Taskbar Height", "Height of the taskbar in logical pixels", TE_SETTING_INT, { .int_val = { 24, 20, 128, 1 } } }
};

static const PluginSettings g_plugin_settings = {
    g_settings,
    1
};

static const PluginMetadata g_metadata = {
    "taskbar_resize",
    "Taskbar Resize",
    "Dynamically resize the Windows taskbar.",
    "TaskbarEngine",
    100,
    10, /* low priority to resize before visual */
    TE_API_VERSION
};

static uint32_t g_current_dpi = 96;

static int TE_ScaleDPI(int value, uint32_t dpi) {
    return (value * (int)dpi) / 96;
}

static RECT g_original_work_area;
static BOOL g_work_area_saved = FALSE;

static void ApplyWorkArea(int new_height) {
    RECT screen_rect;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &screen_rect, 0)) {
        if (!g_work_area_saved) {
            g_original_work_area = screen_rect;
            g_work_area_saved = TRUE;
        }
        
        HMONITOR hMon = (g_ctx && g_ctx->monitor) ? g_ctx->monitor : MonitorFromWindow(g_ctx ? g_ctx->taskbar_hwnd : NULL, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(MONITORINFO) };
        if (GetMonitorInfoW(hMon, &mi)) {
            RECT new_wa = mi.rcMonitor;
            new_wa.bottom -= TE_ScaleDPI(new_height, g_current_dpi);
            SystemParametersInfoW(SPI_SETWORKAREA, 0, &new_wa, SPIF_SENDCHANGE);
        }
    }
}

static void RestoreWorkArea(void) {
    if (g_work_area_saved) {
        SystemParametersInfoW(SPI_SETWORKAREA, 0, &g_original_work_area, SPIF_SENDCHANGE);
        g_work_area_saved = FALSE;
    }
}

static void ResizeChildWindows(HWND parent, int new_height) {
    HWND child = GetWindow(parent, GW_CHILD);
    while (child) {
        RECT cr;
        if (GetWindowRect(child, &cr)) {
            int cw = cr.right - cr.left;
            POINT pt = { cr.left, cr.top };
            ScreenToClient(parent, &pt);
            SetWindowPos(child, NULL, pt.x, 0, cw, new_height, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static void ApplyTaskbarSize(HWND hwnd) {
    if (!hwnd) return;
    int new_cy = TE_ScaleDPI(g_config.height, g_current_dpi);
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (GetMonitorInfoW(hMon, &mi)) {
        int x = mi.rcMonitor.left;
        int y = mi.rcMonitor.bottom - new_cy;
        int cx = mi.rcMonitor.right - mi.rcMonitor.left;
        ResizeLog(TE_LOG_INFO, "ApplyTaskbarSize: rect=(%d, %d, %d, %d), height=%d, dpi=%u",
                  x, y, cx, new_cy, g_config.height, g_current_dpi);
        SetWindowPos(hwnd, NULL, x, y, cx, new_cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        ApplyWorkArea(g_config.height);
        ResizeChildWindows(hwnd, new_cy);
    }
}

static void RestoreTaskbarSize(HWND hwnd) {
    if (!hwnd) return;
    int default_cy = TE_ScaleDPI(48, g_current_dpi);
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (GetMonitorInfoW(hMon, &mi)) {
        int x = mi.rcMonitor.left;
        int y = mi.rcMonitor.bottom - default_cy;
        int cx = mi.rcMonitor.right - mi.rcMonitor.left;
        ResizeLog(TE_LOG_INFO, "RestoreTaskbarSize: rect=(%d, %d, %d, %d)", x, y, cx, default_cy);
        SetWindowPos(hwnd, NULL, x, y, cx, default_cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RestoreWorkArea();
        ResizeChildWindows(hwnd, default_cy);
    }
}

static LRESULT CALLBACK ResizeSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    (void)dwRefData;
    
    switch (msg) {
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
            MONITORINFO mi = { sizeof(MONITORINFO) };
            if (GetMonitorInfoW(hMon, &mi)) {
                wp->y = mi.rcMonitor.bottom - new_cy;
                wp->cy = new_cy;
                wp->x = mi.rcMonitor.left;
                wp->cx = mi.rcMonitor.right - mi.rcMonitor.left;
                wp->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
            }
            return res;
        }
        case WM_WINDOWPOSCHANGED: {
            LRESULT res = DefSubclassProc(hwnd, msg, wParam, lParam);
            int new_cy = TE_ScaleDPI(g_config.height, g_current_dpi);
            ResizeChildWindows(hwnd, new_cy);
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
        g_config.height = height_node->valueint;
    }
}

static void OnConfigChanged(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    const TE_ConfigChangedData* changed = (const TE_ConfigChangedData*)data;
    ParseConfig((const cJSON*)changed->new_config);
    ResizeLog(TE_LOG_INFO, "OnConfigChanged: new height=%d", g_config.height);
    ApplyTaskbarSize(g_ctx->taskbar_hwnd);
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
    
    return TE_S_OK;
}

static HRESULT Disable(void) {
    ResizeLog(TE_LOG_INFO, "Disable: restoring default height");
    RemoveWindowSubclass(g_ctx->taskbar_hwnd, ResizeSubclassProc, SUBCLASS_RESIZE_ID);
    g_ctx->unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged);
    g_ctx->unsubscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged);
    
    RestoreTaskbarSize(g_ctx->taskbar_hwnd);
    
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
