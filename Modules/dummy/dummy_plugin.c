#include <sdk/te_plugin.h>
#include <sdk/te_events.h>
#include <stdio.h>

static const PluginContext* g_ctx = NULL;

static const PluginMetadata G_METADATA = {
    "DummyPlugin",
    "1.0.0",
    "TaskbarEngine",
    "Dummy plugin for Phase 2 end-to-end testing",
    999,
    { 0, 0 }
};

static const SettingDescriptor G_SETTINGS_DESCS[] = {
    {
        "show_icon", "Show Icon", "Toggle icon visibility", TE_SETTING_BOOL,
        .value.b = { true }
    },
    {
        "size", "Icon Size", "Size of icons in pixels", TE_SETTING_INT,
        .value.i = { 32, 16, 64, 4 }
    },
    {
        "scale", "Hover Scale", "Scale factor on mouse hover", TE_SETTING_FLOAT,
        .value.f = { 1.2f, 1.0f, 2.0f, 0.1f }
    }
};

static const PluginSettings G_SETTINGS = {
    G_SETTINGS_DESCS,
    sizeof(G_SETTINGS_DESCS) / sizeof(G_SETTINGS_DESCS[0])
};

static HRESULT OnConfigChanged(uint32_t type, const void* event_data, void* user_data)
{
    (void)type; (void)user_data;
    const TE_ConfigChangedEvent* evt = (const TE_ConfigChangedEvent*)event_data;
    if (g_ctx && g_ctx->log) {
        g_ctx->log(TE_LOG_INFO, "DummyPlugin: Config changed event received (new_config ptr: 0x%p)",
                   evt ? (const void*)evt->new_config : NULL);
    }
    return S_OK;
}

static HRESULT DummyInitialize(const PluginContext* ctx)
{
    g_ctx = ctx;
    if (g_ctx && g_ctx->log) {
        g_ctx->log(TE_LOG_INFO, "DummyPlugin: Initialize called");
    }
    return S_OK;
}

static HRESULT DummyEnable(void)
{
    if (g_ctx && g_ctx->log) {
        g_ctx->log(TE_LOG_INFO, "DummyPlugin: Enable called");
    }
    if (g_ctx && g_ctx->subscribe) {
        g_ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged, NULL);
    }
    return S_OK;
}

static HRESULT DummyDisable(void)
{
    if (g_ctx && g_ctx->log) {
        g_ctx->log(TE_LOG_INFO, "DummyPlugin: Disable called");
    }
    if (g_ctx && g_ctx->unsubscribe) {
        g_ctx->unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged);
    }
    return S_OK;
}

static HRESULT DummyUpdate(float deltaTime)
{
    (void)deltaTime;
    if (g_ctx && g_ctx->log) {
        g_ctx->log(TE_LOG_DEBUG, "DummyPlugin: Update(dt=%.3f)", (double)deltaTime);
    }
    return S_OK;
}

static HRESULT DummyShutdown(void)
{
    if (g_ctx && g_ctx->log) {
        g_ctx->log(TE_LOG_INFO, "DummyPlugin: Shutdown called");
    }
    g_ctx = NULL;
    return S_OK;
}

static const PluginMetadata* DummyGetMetadata(void)
{
    return &G_METADATA;
}

static const PluginSettings* DummyGetSettings(void)
{
    return &G_SETTINGS;
}

static const PluginInterface G_INTERFACE = {
    DummyInitialize,
    DummyEnable,
    DummyDisable,
    DummyUpdate,
    DummyShutdown,
    DummyGetMetadata,
    DummyGetSettings
};

TE_EXPORT const PluginInterface* GetPluginInterface(void)
{
    return &G_INTERFACE;
}
