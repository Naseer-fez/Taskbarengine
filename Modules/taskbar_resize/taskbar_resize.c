#include <sdk/te_plugin.h>
#include <sdk/te_events.h>
#include <sdk/te_jsonc.h>
#include <cJSON.h>
#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>

#define SUBCLASS_RESIZE_ID 0x5442525A /* 'TBRZ' */

static const PluginContext* g_ctx = NULL;

static struct {
    int height;
} g_config = { 48 };

static const SettingDescriptor g_settings[] = {
    { "height", "Taskbar Height", "Height of the taskbar in logical pixels", TE_SETTING_INT, { .int_val = { 48, 24, 128, 1 } } }
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
        
        MONITORINFO mi = { sizeof(MONITORINFO) };
        if (GetMonitorInfoW(g_ctx->monitor, &mi)) {
            /* Basic assumption: bottom taskbar on primary monitor */
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

static LRESULT CALLBACK ResizeSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    (void)dwRefData;
    
    switch (msg) {
        case WM_WINDOWPOSCHANGING: {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            if (!(wp->flags & SWP_NOSIZE)) {
                wp->cy = TE_ScaleDPI(g_config.height, g_current_dpi);
            }
            break;
        }
        case WM_DPICHANGED: {
            g_current_dpi = HIWORD(wParam);
            SetWindowPos(hwnd, NULL, 0, 0, 0, TE_ScaleDPI(g_config.height, g_current_dpi),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            ApplyWorkArea(g_config.height);
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
    
    SetWindowPos(g_ctx->taskbar_hwnd, NULL, 0, 0, 0, TE_ScaleDPI(g_config.height, g_current_dpi),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ApplyWorkArea(g_config.height);
}

static void OnDpiChanged(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    const TE_DpiChangedData* dpi_data = (const TE_DpiChangedData*)data;
    g_current_dpi = dpi_data->new_dpi;
    SetWindowPos(g_ctx->taskbar_hwnd, NULL, 0, 0, 0, TE_ScaleDPI(g_config.height, g_current_dpi),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ApplyWorkArea(g_config.height);
}

static HRESULT Initialize(const PluginContext* ctx) {
    g_ctx = ctx;
    g_current_dpi = ctx->dpi;
    ParseConfig(g_ctx->config);
    return TE_S_OK;
}

static HRESULT Enable(void) {
    SetWindowSubclass(g_ctx->taskbar_hwnd, ResizeSubclassProc, SUBCLASS_RESIZE_ID, 0);
    g_ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged, NULL);
    g_ctx->subscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged, NULL);
    
    SetWindowPos(g_ctx->taskbar_hwnd, NULL, 0, 0, 0, TE_ScaleDPI(g_config.height, g_current_dpi),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ApplyWorkArea(g_config.height);
    
    return TE_S_OK;
}

static HRESULT Disable(void) {
    RemoveWindowSubclass(g_ctx->taskbar_hwnd, ResizeSubclassProc, SUBCLASS_RESIZE_ID);
    g_ctx->unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged);
    g_ctx->unsubscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged);
    
    /* Restore to default size... Windows default is ~48px at 100% DPI */
    RestoreWorkArea();
    SetWindowPos(g_ctx->taskbar_hwnd, NULL, 0, 0, 0, TE_ScaleDPI(48, g_ctx->dpi),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    
    return TE_S_OK;
}

static HRESULT Update(float delta_time) {
    (void)delta_time;
    return TE_S_OK;
}

static HRESULT Shutdown(void) {
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
