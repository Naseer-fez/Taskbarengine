#include <sdk/te_plugin.h>
#include <sdk/te_log.h>
#include <sdk/te_events.h>
#include <sdk/te_types.h>

static const PluginContext* g_ctx = NULL;

static void DummyLog(const char* message) {
    if (g_ctx && g_ctx->log) {
        g_ctx->log(TE_LOG_INFO, "DummyPlugin", message);
    }
}

static void DummyOnConfigChanged(uint32_t event_type, const void* event_data, void* user_data) {
    (void)event_type;
    (void)event_data;
    (void)user_data;
    DummyLog("Received CONFIG_CHANGED event");
}

static HRESULT DummyInitialize(const PluginContext* ctx) {
    g_ctx = ctx;
    DummyLog("Initialize() called");
    return TE_S_OK;
}

static HRESULT DummyEnable(void) {
    DummyLog("Enable() called");
    if (g_ctx && g_ctx->subscribe) {
        g_ctx->subscribe(TE_EVENT_CONFIG_CHANGED, DummyOnConfigChanged, NULL);
    }
    return TE_S_OK;
}

static HRESULT DummyDisable(void) {
    DummyLog("Disable() called");
    if (g_ctx && g_ctx->unsubscribe) {
        g_ctx->unsubscribe(TE_EVENT_CONFIG_CHANGED, DummyOnConfigChanged);
    }
    return TE_S_OK;
}

static HRESULT DummyUpdate(float delta_time) {
    (void)delta_time;
    return TE_S_OK;
}

static HRESULT DummyShutdown(void) {
    DummyLog("Shutdown() called");
    g_ctx = NULL;
    return TE_S_OK;
}

static const PluginMetadata g_metadata = {
    "dummy",
    "Dummy Test Plugin",
    "A test plugin that logs all lifecycle calls.",
    "TaskbarEngine",
    100,    /* version 1.0.0 */
    999,    /* priority: load last */
    TE_API_VERSION
};

static const PluginMetadata* DummyGetMetadata(void) {
    return &g_metadata;
}

static const PluginSettings* DummyGetSettings(void) {
    return NULL;  /* No configurable settings */
}

static const PluginInterface g_interface = {
    DummyInitialize,
    DummyEnable,
    DummyDisable,
    DummyUpdate,
    DummyShutdown,
    DummyGetMetadata,
    DummyGetSettings
};

TE_EXPORT const PluginInterface* GetPluginInterface(void) {
    return &g_interface;
}
