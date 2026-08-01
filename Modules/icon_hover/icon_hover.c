#include "icon_hover_internal.h"
#include "dcomp_overlay.h"
#include "frame_loop.h"
#include <sdk/te_events.h>
#include <string.h>

static TE_IconHoverState g_state = { 0 };

static const PluginMetadata g_metadata = {
    "icon_hover",
    "0.4.0",
    "TaskbarEngine",
    "macOS Dock-style smooth icon magnification on taskbar hover.",
    100
};

static const SettingDescriptor g_descriptors[] = {
    { "scale", "Max scale", "Maximum magnification scale (e.g. 1.3).", TE_SETTING_FLOAT, { .f = { 1.30f, 1.00f, 2.00f, 0.05f } } },
    { "radius", "Hover radius", "Magnification influence radius in pixels.", TE_SETTING_INT, { .i = { 120, 40, 300, 10 } } },
    { "curve", "Curve type", "Falloff curve (0=Gaussian, 1=Cosine, 2=Linear, 3=Cubic).", TE_SETTING_INT, { .i = { 0, 0, 3, 1 } } },
    { "speed_ms", "Animation speed", "Transition and settle duration in milliseconds.", TE_SETTING_INT, { .i = { 150, 50, 500, 10 } } }
};

static const PluginSettings g_settings_schema = {
    g_descriptors,
    sizeof(g_descriptors) / sizeof(g_descriptors[0])
};

static void LoadSettings(const cJSON* config)
{
    g_state.max_scale = 1.30f;
    g_state.radius = 120;
    g_state.curve = TE_CURVE_GAUSSIAN;
    g_state.speed_ms = 150;

    if (!config) return;

    const cJSON* item = cJSON_GetObjectItemCaseSensitive(config, "scale");
    if (item && cJSON_IsNumber(item)) g_state.max_scale = (float)item->valuedouble;

    item = cJSON_GetObjectItemCaseSensitive(config, "radius");
    if (item && cJSON_IsNumber(item)) g_state.radius = item->valueint;

    item = cJSON_GetObjectItemCaseSensitive(config, "curve");
    if (item && cJSON_IsNumber(item)) g_state.curve = (TE_MagnifyCurveType)item->valueint;

    item = cJSON_GetObjectItemCaseSensitive(config, "speed_ms");
    if (item && cJSON_IsNumber(item)) g_state.speed_ms = item->valueint;
}

static HRESULT OnEvent(uint32_t type, const void* event_data, void* user_data)
{
    (void)user_data;

    switch ((TE_EventType)type) {
        case TE_EVENT_CONFIG_CHANGED: {
            const TE_ConfigChangedEvent* evt = (const TE_ConfigChangedEvent*)event_data;
            LoadSettings(evt ? evt->new_config : g_state.ctx.config);
            break;
        }

        case TE_EVENT_TASKBAR_GEOMETRY: {
            if (g_state.enabled && !TE_FrameLoopIsRunning()) {
                TE_FrameLoopStart(&g_state);
            }
            break;
        }

        case TE_EVENT_SHELL_HOOK: {
            if (g_state.enabled && g_state.ctx.taskbar_hwnd) {
                int count = 0;
                TE_UiaDiscoverIcons(g_state.ctx.taskbar_hwnd, g_state.icons, TE_MAX_TASKBAR_ICONS, &count);
                g_state.icon_count = count;
                TE_DCompOverlaySetIcons(g_state.icons, NULL, g_state.icon_count);
            }
            break;
        }

        default:
            break;
    }

    return S_OK;
}

static HRESULT PluginInitialize(const PluginContext* ctx)
{
    if (!ctx) return E_POINTER;

    memset(&g_state, 0, sizeof(g_state));
    g_state.ctx = *ctx;
    LoadSettings(ctx->config);

    TE_IconCaptureInit();

    if (ctx->subscribe) {
        ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnEvent, NULL);
        ctx->subscribe(TE_EVENT_TASKBAR_GEOMETRY, OnEvent, NULL);
        ctx->subscribe(TE_EVENT_SHELL_HOOK, OnEvent, NULL);
    }

    g_state.initialized = true;
    return S_OK;
}

static HRESULT PluginEnable(void)
{
    if (!g_state.initialized) return E_UNEXPECTED;
    g_state.enabled = true;

    if (g_state.ctx.taskbar_hwnd) {
        TE_DCompOverlayCreate(g_state.ctx.taskbar_hwnd, g_state.ctx.monitor);
        int count = 0;
        TE_UiaDiscoverIcons(g_state.ctx.taskbar_hwnd, g_state.icons, TE_MAX_TASKBAR_ICONS, &count);
        g_state.icon_count = count;
        TE_DCompOverlaySetIcons(g_state.icons, NULL, g_state.icon_count);
    }

    TE_FrameLoopStart(&g_state);
    return S_OK;
}

static HRESULT PluginDisable(void)
{
    if (!g_state.enabled) return S_OK;
    g_state.enabled = false;
    TE_FrameLoopStop();
    TE_DCompOverlayDestroy();
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
        g_state.ctx.unsubscribe(TE_EVENT_CONFIG_CHANGED, OnEvent);
        g_state.ctx.unsubscribe(TE_EVENT_SHELL_HOOK, OnEvent);
    }
    TE_IconCaptureShutdown();
    TE_UiaCleanup();
    memset(&g_state, 0, sizeof(g_state));
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
