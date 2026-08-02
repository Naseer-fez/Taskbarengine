#include <sdk/te_plugin.h>
#include <sdk/te_log.h>
#include <sdk/te_events.h>
#include "icon_hover_internal.h"
#include "uia_discovery.h"
#include "dcomp_overlay.h"
#include "icon_capture.h"
#include "frame_loop.h"
#include <string.h>

static TE_IconHoverState g_state = {0};

static const PluginMetadata g_metadata = {
    "icon_hover",
    "0.4.0",
    "TaskbarEngine",
    "Magnifies taskbar icons on hover using DirectComposition and smooth animation curves.",
    5
};

static const char* g_curve_options[] = { "gaussian", "cubic", "linear", "cosine" };

static const SettingDescriptor g_descriptors[] = {
    { "scale", "Scale", "Maximum icon scale.", TE_SETTING_FLOAT, { .f = { 1.3f, 1.0f, 2.0f, 0.1f } } },
    { "radius", "Radius", "Magnification radius in px.", TE_SETTING_INT, { .i = { 120, 40, 300, 10 } } },
    { "curve", "Curve", "Magnification falloff curve.", TE_SETTING_ENUM, { .e = { "gaussian", g_curve_options, 4 } } },
    { "speed_ms", "Speed", "Settle animation speed in ms.", TE_SETTING_INT, { .i = { 150, 50, 500, 10 } } }
};

static const PluginSettings g_settings_schema = {
    g_descriptors,
    sizeof(g_descriptors) / sizeof(g_descriptors[0])
};

static void ParseConfig(const cJSON* config)
{
    if (!config) return;
    const cJSON* scale = cJSON_GetObjectItemCaseSensitive(config, "scale");
    if (scale && cJSON_IsNumber(scale)) {
        g_state.max_scale = (float)scale->valuedouble;
    }
    const cJSON* radius = cJSON_GetObjectItemCaseSensitive(config, "radius");
    if (radius && cJSON_IsNumber(radius)) {
        g_state.radius = radius->valueint;
    }
    const cJSON* speed = cJSON_GetObjectItemCaseSensitive(config, "speed_ms");
    if (speed && cJSON_IsNumber(speed)) {
        g_state.speed_ms = speed->valueint;
    }
    const cJSON* curve = cJSON_GetObjectItemCaseSensitive(config, "curve");
    if (curve && cJSON_IsString(curve) && curve->valuestring) {
        if (strcmp(curve->valuestring, "gaussian") == 0) g_state.curve = TE_CURVE_GAUSSIAN;
        else if (strcmp(curve->valuestring, "cubic") == 0) g_state.curve = TE_CURVE_CUBIC;
        else if (strcmp(curve->valuestring, "linear") == 0) g_state.curve = TE_CURVE_LINEAR;
        else if (strcmp(curve->valuestring, "cosine") == 0) g_state.curve = TE_CURVE_COSINE;
    }
}

static HRESULT OnConfigChanged(uint32_t event_type, const void* event_data, void* user_data)
{
    (void)event_type; (void)user_data;
    if (event_data) {
        const TE_ConfigChangedEvent* evt = (const TE_ConfigChangedEvent*)event_data;
        ParseConfig(evt->new_config);
    }
    return S_OK;
}

static HRESULT OnShellHook(uint32_t event_type, const void* event_data, void* user_data)
{
    (void)event_type; (void)event_data; (void)user_data;
    if (g_state.enabled) {
        HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
        if (taskbar_hwnd) {
            AcquireSRWLockExclusive(&g_state.icon_lock);
            TE_UiaDiscoverIcons(taskbar_hwnd, g_state.icons, TE_MAX_TASKBAR_ICONS, &g_state.icon_count);
            ReleaseSRWLockExclusive(&g_state.icon_lock);
        }
    }
    return S_OK;
}

static HRESULT OnDpiChanged(uint32_t event_type, const void* event_data, void* user_data)
{
    (void)event_type; (void)user_data;
    if (event_data) {
        const TE_DpiChangedEvent* evt = (const TE_DpiChangedEvent*)event_data;
        g_state.ctx.dpi = evt->new_dpi;
    }
    return S_OK;
}

static BOOL CALLBACK EnumSecondaryTaskbars(HWND hwnd, LPARAM lParam)
{
    wchar_t class_name[64] = {0};
    GetClassNameW(hwnd, class_name, 64);
    if (wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0) {
        TE_IconHoverState* state = (TE_IconHoverState*)lParam;
        if (state->secondary_count < sizeof(state->secondary_taskbars) / sizeof(state->secondary_taskbars[0])) {
            state->secondary_taskbars[state->secondary_count++] = hwnd;
        }
    }
    return TRUE;
}

static HRESULT IconHover_Init(const PluginContext* ctx) {
    if (!ctx) return E_POINTER;
    g_state.ctx = *ctx;
    g_state.max_scale = 1.3f;
    g_state.radius = 120;
    g_state.speed_ms = 150;
    g_state.curve = TE_CURVE_GAUSSIAN;
    InitializeSRWLock(&g_state.icon_lock);

    ParseConfig(ctx->config);

    if (ctx->subscribe) {
        ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged, NULL);
        ctx->subscribe(TE_EVENT_SHELL_HOOK, OnShellHook, NULL);
        ctx->subscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged, NULL);
    }
    
    InterlockedExchange(&g_state.initialized, 1);
    TE_LogWrite(TE_LOG_INFO, "IconHover plugin initialized");
    return S_OK;
}

static HRESULT IconHover_Enable(void) {
    if (!g_state.initialized) return E_FAIL;
    
    HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!taskbar_hwnd) return E_FAIL;

    g_state.secondary_count = 0;
    EnumWindows(EnumSecondaryTaskbars, (LPARAM)&g_state);

    TE_IconCaptureInit();

    AcquireSRWLockExclusive(&g_state.icon_lock);
    TE_UiaDiscoverIcons(taskbar_hwnd, g_state.icons, TE_MAX_TASKBAR_ICONS, &g_state.icon_count);
    ReleaseSRWLockExclusive(&g_state.icon_lock);

    HRESULT hr = TE_DcompInit(taskbar_hwnd);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "IconHover DComp Init failed: 0x%08X", hr);
        return hr;
    }

    RECT tb_rect;
    GetWindowRect(taskbar_hwnd, &tb_rect);
    g_state.taskbar_rect = tb_rect;

    int tb_height = tb_rect.bottom - tb_rect.top;
    if (g_state.ctx.query_state) {
        StateValue height_val;
        if (SUCCEEDED(g_state.ctx.query_state("taskbar_resize.height", &height_val)) && height_val.type == TE_STATE_TYPE_INT) {
            tb_height = height_val.value.i;
        }
    }
    g_state.icon_size = (float)tb_height * 0.70f;
    if (g_state.icon_size < 24.0f) g_state.icon_size = 24.0f;

    hr = TE_FrameLoopStart(&g_state);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "IconHover FrameLoopStart failed: 0x%08X", hr);
        TE_DcompShutdown();
        return hr;
    }

    InterlockedExchange(&g_state.enabled, 1);
    TE_LogWrite(TE_LOG_INFO, "IconHover plugin enabled");
    return S_OK;
}

static HRESULT IconHover_Disable(void) {
    InterlockedExchange(&g_state.enabled, 0);
    
    TE_FrameLoopStop();
    TE_DcompShutdown();
    TE_IconCaptureShutdown();
    TE_UiaCleanup();
    
    AcquireSRWLockExclusive(&g_state.icon_lock);
    g_state.icon_count = 0;
    ReleaseSRWLockExclusive(&g_state.icon_lock);

    TE_LogWrite(TE_LOG_INFO, "IconHover plugin disabled");
    return S_OK;
}

static HRESULT IconHover_Update(float deltaTime) {
    (void)deltaTime;
    return S_OK;
}

static HRESULT IconHover_Shutdown(void) {
    if (g_state.enabled) {
        IconHover_Disable();
    }
    if (g_state.ctx.unsubscribe) {
        g_state.ctx.unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged);
        g_state.ctx.unsubscribe(TE_EVENT_SHELL_HOOK, OnShellHook);
        g_state.ctx.unsubscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged);
    }
    InterlockedExchange(&g_state.initialized, 0);
    return S_OK;
}

static const PluginMetadata* IconHover_GetMetadata(void) {
    return &g_metadata;
}

static const PluginSettings* IconHover_GetSettings(void) {
    return &g_settings_schema;
}

static const PluginInterface g_plugin = {
    IconHover_Init,
    IconHover_Enable,
    IconHover_Disable,
    IconHover_Update,
    IconHover_Shutdown,
    IconHover_GetMetadata,
    IconHover_GetSettings
};

TE_EXPORT const PluginInterface* GetPluginInterface(void) {
    return &g_plugin;
}
