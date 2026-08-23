#include "taskbar_resize.h"
#include <sdk/te_events.h>
#include <commctrl.h>

typedef struct TE_TaskbarResizeSettings {
    int height;
    int padding;
    int margins;
    int icon_spacing;
} TE_TaskbarResizeSettings;

typedef struct TE_TaskbarBarState {
    HWND hwnd;
    HMONITOR monitor;
    RECT original_rect;
    RECT original_work_area;
    bool saved_original;
    bool updates_work_area;
} TE_TaskbarBarState;

typedef struct TE_TaskbarResizeState {
    PluginContext ctx;
    bool initialized;
    bool enabled;
    HWND taskbar_hwnd;
    HMONITOR monitor;
    TE_TaskbarResizeSettings settings;
    TE_TaskbarBarState bars[8];
    uint32_t bar_count;
} TE_TaskbarResizeState;

static TE_TaskbarResizeState g_state = { 0 };

static const PluginMetadata g_metadata = {
    "taskbar_resize",
    "0.3.0",
    "TaskbarEngine",
    "Resizes the Windows taskbar and restores it on disable.",
    10,
    { 22621, 26100 }
};

static const SettingDescriptor g_descriptors[] = {
    { "height", "Height", "Taskbar height in logical pixels.", TE_SETTING_INT, { .i = { TE_TASKBAR_RESIZE_DEFAULT_HEIGHT, TE_TASKBAR_RESIZE_MIN_HEIGHT, TE_TASKBAR_RESIZE_MAX_HEIGHT, 1 } } },
    { "padding", "Padding", "Reserved padding around taskbar content.", TE_SETTING_INT, { .i = { 4, 0, 20, 1 } } },
    { "margins", "Margins", "Reserved taskbar edge margin.", TE_SETTING_INT, { .i = { 0, 0, 20, 1 } } },
    { "icon_spacing", "Icon spacing", "Reserved spacing between taskbar icons.", TE_SETTING_INT, { .i = { 8, 0, 32, 1 } } }
};

static const PluginSettings g_settings_schema = {
    g_descriptors,
    sizeof(g_descriptors) / sizeof(g_descriptors[0])
};

int TE_TaskbarResizeClampHeight(int height)
{
    if (height < TE_TASKBAR_RESIZE_MIN_HEIGHT) return TE_TASKBAR_RESIZE_MIN_HEIGHT;
    if (height > TE_TASKBAR_RESIZE_MAX_HEIGHT) return TE_TASKBAR_RESIZE_MAX_HEIGHT;
    return height;
}

int TE_TaskbarResizeScaleForDpi(int value, uint32_t dpi)
{
    if (dpi == 0) dpi = 96;
    return MulDiv(value, (int)dpi, 96);
}

static int ConfigReadInt(const cJSON* config, const char* key, int fallback)
{
    const cJSON* item = config ? cJSON_GetObjectItemCaseSensitive(config, key) : NULL;
    if (!item || !cJSON_IsNumber(item)) return fallback;
    return item->valueint;
}

static void LoadSettings(const cJSON* config)
{
    g_state.settings.height = TE_TaskbarResizeClampHeight(ConfigReadInt(config, "height", TE_TASKBAR_RESIZE_DEFAULT_HEIGHT));
    g_state.settings.padding = max(0, min(20, ConfigReadInt(config, "padding", 4)));
    g_state.settings.margins = max(0, min(20, ConfigReadInt(config, "margins", 0)));
    g_state.settings.icon_spacing = max(0, min(32, ConfigReadInt(config, "icon_spacing", 8)));
}

static uint32_t CurrentDpi(void)
{
    return g_state.ctx.dpi ? g_state.ctx.dpi : 96;
}

static uint32_t WindowDpi(HWND hwnd)
{
    uint32_t dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    return dpi ? dpi : CurrentDpi();
}

static TE_TaskbarBarState* FindOrAddBar(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return NULL;

    for (uint32_t i = 0; i < g_state.bar_count; i++) {
        if (g_state.bars[i].hwnd == hwnd) {
            return &g_state.bars[i];
        }
    }

    if (g_state.bar_count >= sizeof(g_state.bars) / sizeof(g_state.bars[0])) return NULL;

    TE_TaskbarBarState* bar = &g_state.bars[g_state.bar_count++];
    ZeroMemory(bar, sizeof(*bar));
    bar->hwnd = hwnd;
    bar->monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    return bar;
}

static void SaveOriginalGeometry(TE_TaskbarBarState* bar)
{
    if (!bar || bar->saved_original || !bar->hwnd || !IsWindow(bar->hwnd)) return;

    GetWindowRect(bar->hwnd, &bar->original_rect);

    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    bar->monitor = MonitorFromWindow(bar->hwnd, MONITOR_DEFAULTTONEAREST);
    if (bar->monitor && GetMonitorInfoW(bar->monitor, &mi)) {
        bar->original_work_area = mi.rcWork;
    }

    bar->saved_original = true;
}

static void ApplyWorkArea(TE_TaskbarBarState* bar, const RECT* taskbar_rect)
{
    if (!taskbar_rect) return;

    /* Validate: reject obviously invalid rects (e.g. during XAML partial layout) */
    if (taskbar_rect->top <= 0 || taskbar_rect->bottom <= taskbar_rect->top) return;

    RECT work;
    ZeroMemory(&work, sizeof(work));
    if (bar) {
        work = bar->original_work_area;
    }
    if (IsRectEmpty(&work)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    work.bottom = taskbar_rect->top;
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &work, SPIF_UPDATEINIFILE);
    DWORD_PTR res = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, (LPARAM)L"WindowsMetrics", SMTO_ABORTIFHUNG, 100, &res);
}

static void ApplyHeightToWindow(HWND hwnd, bool update_work_area)
{
    if (!g_state.enabled || !hwnd || !IsWindow(hwnd)) return;

    TE_TaskbarBarState* bar = FindOrAddBar(hwnd);
    SaveOriginalGeometry(bar);
    if (bar) bar->updates_work_area = update_work_area;

    RECT rc;
    GetWindowRect(hwnd, &rc);

    int target_height = TE_TaskbarResizeScaleForDpi(g_state.settings.height, WindowDpi(hwnd));
    int width = rc.right - rc.left;
    rc.top = rc.bottom - target_height;

    /* SWP_FRAMECHANGED removed: it triggers WM_NCCALCSIZE which causes the
     * XAML layout engine to fight our height override via re-entrant
     * WM_WINDOWPOSCHANGING messages, creating a feedback loop. */
    SetWindowPos(hwnd, NULL, rc.left, rc.top, width, target_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (bar && bar->updates_work_area) {
        ApplyWorkArea(bar, &rc);
    }
}

static void ApplyHeight(void)
{
    HWND primary = FindWindowW(L"Shell_TrayWnd", NULL);
    ApplyHeightToWindow(primary ? primary : g_state.taskbar_hwnd, true);

    HWND secondary = NULL;
    while ((secondary = FindWindowExW(NULL, secondary, L"Shell_SecondaryTrayWnd", NULL)) != NULL) {
        ApplyHeightToWindow(secondary, false);
    }

    if (g_state.ctx.publish_state) {
        StateValue val;
        val.type = TE_STATE_TYPE_INT;
        val.value.i = TE_TaskbarResizeScaleForDpi(g_state.settings.height, CurrentDpi());
        g_state.ctx.publish_state("taskbar_resize.height", &val);
    }
}

static void RestoreGeometry(void)
{
    for (uint32_t i = 0; i < g_state.bar_count; i++) {
        TE_TaskbarBarState* bar = &g_state.bars[i];
        if (!bar->saved_original || !bar->hwnd || !IsWindow(bar->hwnd)) continue;

        int width = bar->original_rect.right - bar->original_rect.left;
        int height = bar->original_rect.bottom - bar->original_rect.top;
        SetWindowPos(bar->hwnd, NULL, bar->original_rect.left, bar->original_rect.top,
                     width, height, SWP_NOZORDER | SWP_NOACTIVATE);

        if (bar->updates_work_area && !IsRectEmpty(&bar->original_work_area)) {
            SystemParametersInfoW(SPI_SETWORKAREA, 0, &bar->original_work_area, SPIF_UPDATEINIFILE);
            DWORD_PTR res = 0;
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, (LPARAM)L"WindowsMetrics", SMTO_ABORTIFHUNG, 100, &res);
        }
    }
}

void TE_TaskbarResizeEnforceWindowPos(WINDOWPOS* wp, int target_height, uint32_t dpi)
{
    if (!wp) return;

    int old_cy = wp->cy;
    int scaled_height = TE_TaskbarResizeScaleForDpi(target_height, dpi);
    wp->cy = scaled_height;
    wp->y += old_cy - scaled_height;
}

static void EnforceWindowPos(WINDOWPOS* wp, HWND taskbar_hwnd)
{
    if (!g_state.enabled || !wp) return;

    TE_TaskbarResizeEnforceWindowPos(wp, g_state.settings.height, WindowDpi(taskbar_hwnd));
}

static HRESULT OnEvent(uint32_t type, const void* event_data, void* user_data)
{
    (void)user_data;

    switch ((TE_EventType)type) {
        case TE_EVENT_TASKBAR_GEOMETRY: {
            const TE_TaskbarGeometryEvent* evt = (const TE_TaskbarGeometryEvent*)event_data;
            if (evt) {
                g_state.taskbar_hwnd = evt->taskbar_hwnd;
                FindOrAddBar(evt->taskbar_hwnd);
                EnforceWindowPos(evt->window_pos, evt->taskbar_hwnd);
            }
            break;
        }

        case TE_EVENT_CONFIG_CHANGED: {
            const TE_ConfigChangedEvent* evt = (const TE_ConfigChangedEvent*)event_data;
            if (evt) g_state.ctx.config = evt->new_config;
            LoadSettings(evt ? evt->new_config : g_state.ctx.config);
            ApplyHeight();
            break;
        }

        case TE_EVENT_DPI_CHANGED: {
            const TE_DpiChangedEvent* evt = (const TE_DpiChangedEvent*)event_data;
            if (evt) {
                g_state.ctx.dpi = evt->new_dpi;
            }
            ApplyHeight();
            break;
        }

        case TE_EVENT_DISPLAY_CHANGED:
            ApplyHeight();
            break;

        default:
            break;
    }

    return S_OK;
}

static HRESULT PluginInitialize(const PluginContext* ctx)
{
    if (!ctx) return E_POINTER;

    ZeroMemory(&g_state, sizeof(g_state));
    g_state.ctx = *ctx;
    g_state.taskbar_hwnd = ctx->taskbar_hwnd;
    g_state.monitor = ctx->monitor;
    LoadSettings(ctx->config);

    if (ctx->subscribe) {
        ctx->subscribe(TE_EVENT_TASKBAR_GEOMETRY, OnEvent, NULL);
        ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnEvent, NULL);
        ctx->subscribe(TE_EVENT_DPI_CHANGED, OnEvent, NULL);
        ctx->subscribe(TE_EVENT_DISPLAY_CHANGED, OnEvent, NULL);
    }

    g_state.initialized = true;
    return S_OK;
}

static HRESULT PluginEnable(void)
{
    if (!g_state.initialized) return E_UNEXPECTED;
    g_state.enabled = true;
    ApplyHeight();
    return S_OK;
}

static HRESULT PluginDisable(void)
{
    if (!g_state.enabled && g_state.bar_count == 0) return S_OK;
    g_state.enabled = false;
    RestoreGeometry();
    g_state.bar_count = 0;
    return S_OK;
}

static HRESULT PluginUpdate(float deltaTime)
{
    (void)deltaTime;
    return S_OK;
}

static HRESULT PluginShutdown(void)
{
    PluginDisable();
    if (g_state.ctx.unsubscribe) {
        g_state.ctx.unsubscribe(TE_EVENT_TASKBAR_GEOMETRY, OnEvent);
        g_state.ctx.unsubscribe(TE_EVENT_CONFIG_CHANGED, OnEvent);
        g_state.ctx.unsubscribe(TE_EVENT_DPI_CHANGED, OnEvent);
        g_state.ctx.unsubscribe(TE_EVENT_DISPLAY_CHANGED, OnEvent);
    }
    ZeroMemory(&g_state, sizeof(g_state));
    return S_OK;
}

static const PluginMetadata* PluginGetMetadata(void)
{
    return &g_metadata;
}

static const PluginSettings* PluginGetSettings(void)
{
    return &g_settings_schema;
}

static const PluginInterface g_interface = {
    PluginInitialize,
    PluginEnable,
    PluginDisable,
    PluginUpdate,
    PluginShutdown,
    PluginGetMetadata,
    PluginGetSettings
};

TE_EXPORT const PluginInterface* GetPluginInterface(void)
{
    return &g_interface;
}
