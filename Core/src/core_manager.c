#include "core/core_manager.h"
#include "core/config.h"
#include "core/config_watcher.h"
#include "core/event_dispatch.h"
#include "core/plugin_loader.h"
#include "core/taskbar_subclass.h"
#include <sdk/te_log.h>
#include <sdk/te_log_impl.h>
#include <sdk/te_events.h>
#include <stdio.h>
#include <wchar.h>

struct TE_CoreState {
    HINSTANCE      hinstance;
    HWND           taskbar_hwnd;
    wchar_t        config_path[MAX_PATH];
    wchar_t        modules_dir[MAX_PATH];
    cJSON*         config_root;
    TE_PluginEntry plugins[TE_MAX_PLUGINS];
    uint32_t       plugin_count;
    TE_EventEntry  subscriptions[TE_MAX_SUBSCRIPTIONS];
    uint32_t       subscription_count;
};

static TE_CoreState* g_core_state = NULL;

static HRESULT CoreSubscribeWrapper(uint32_t event_type, EventCallbackFunc callback, void* user_data)
{
    if (!g_core_state) return E_POINTER;
    return TE_EventSubscribe(g_core_state->subscriptions, &g_core_state->subscription_count,
                             (TE_EventType)event_type, (TE_EventCallback)callback, user_data, 0);
}

static HRESULT CoreUnsubscribeWrapper(uint32_t event_type, EventCallbackFunc callback)
{
    if (!g_core_state) return E_POINTER;
    return TE_EventUnsubscribe(g_core_state->subscriptions, &g_core_state->subscription_count,
                               (TE_EventType)event_type, (TE_EventCallback)callback);
}

static void CoreRequestRedrawNoop(void) {}

static HRESULT CorePublishStateNoop(const char* key, const StateValue* val)
{
    (void)key; (void)val;
    return S_OK;
}

static HRESULT CoreQueryStateNoop(const char* key, StateValue* out_val)
{
    (void)key; (void)out_val;
    return E_NOTIMPL;
}

HRESULT TE_CoreManagerInit(HINSTANCE hinstance)
{
    if (g_core_state) return S_OK;

    g_core_state = (TE_CoreState*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TE_CoreState));
    if (!g_core_state) return E_OUTOFMEMORY;

    g_core_state->hinstance = hinstance;
    g_core_state->taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);

    TE_ConfigResolvePath(g_core_state->config_path, MAX_PATH);

    /* Initialize Logging */
    wchar_t log_dir[MAX_PATH];
    wcscpy(log_dir, g_core_state->config_path);
    wchar_t* last_slash = wcsrchr(log_dir, L'\\');
    if (last_slash) {
        wcscpy(last_slash + 1, L"logs");
    } else {
        wcscpy(log_dir, L"logs");
    }
    TE_LogInit(log_dir, TE_LOG_DEBUG, true);

    TE_LogWrite(TE_LOG_INFO, "Core Manager initializing...");

    /* Load Configuration */
    HRESULT hr = TE_ConfigLoad(g_core_state->config_path, &g_core_state->config_root);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_WARN, "Failed to load config, starting with empty config");
    }

    /* Resolve Modules Directory */
    wchar_t dll_path[MAX_PATH];
    DWORD len = GetModuleFileNameW(hinstance, dll_path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        wchar_t* slash = wcsrchr(dll_path, L'\\');
        if (slash) {
            *slash = L'\0';
            swprintf(g_core_state->modules_dir, MAX_PATH, L"%s\\Modules", dll_path);
        }
    }
    if (g_core_state->modules_dir[0] == L'\0') {
        wcscpy(g_core_state->modules_dir, L"Modules");
    }

    /* Initialize Event Dispatch Table */
    TE_EventDispatchInit(g_core_state->subscriptions, &g_core_state->subscription_count);

    /* Install Taskbar Subclass */
    if (g_core_state->taskbar_hwnd) {
        TE_TaskbarSubclassInstall(g_core_state->taskbar_hwnd, g_core_state->subscriptions,
                                 &g_core_state->subscription_count, g_core_state);
    }

    /* Start Config Directory Watcher */
    wchar_t config_dir[MAX_PATH];
    wcscpy(config_dir, g_core_state->config_path);
    last_slash = wcsrchr(config_dir, L'\\');
    if (last_slash) *last_slash = L'\0';
    TE_ConfigWatcherStart(config_dir, g_core_state->taskbar_hwnd);

    /* Discover & Load Plugins */
    TE_PluginLoaderScan(g_core_state->modules_dir, g_core_state->plugins, &g_core_state->plugin_count);

    /* Initialize and Enable Plugins */
    for (uint32_t i = 0; i < g_core_state->plugin_count; i++) {
        TE_PluginEntry* plugin = &g_core_state->plugins[i];
        if (!plugin->context) continue;

        plugin->context->api_version = TE_API_VERSION;
        plugin->context->taskbar_hwnd = g_core_state->taskbar_hwnd;
        plugin->context->monitor = g_core_state->taskbar_hwnd ? MonitorFromWindow(g_core_state->taskbar_hwnd, MONITOR_DEFAULTTONEAREST) : NULL;
        plugin->context->dpi = 96; /* Default DPI */
        plugin->context->config = TE_ConfigGetPluginSection(g_core_state->config_root, plugin->metadata->name);
        plugin->context->log = TE_LogWrite;
        plugin->context->subscribe = CoreSubscribeWrapper;
        plugin->context->unsubscribe = CoreUnsubscribeWrapper;
        plugin->context->request_redraw = CoreRequestRedrawNoop;
        plugin->context->publish_state = CorePublishStateNoop;
        plugin->context->query_state = CoreQueryStateNoop;
        plugin->context->core_opaque = (void*)(uintptr_t)i;

        if (plugin->interface->Initialize) {
            plugin->interface->Initialize(plugin->context);
        }

        /* Check enabled setting in config */
        bool enabled = true; /* Default enabled */
        const cJSON* pcfg = plugin->context->config;
        if (pcfg) {
            const cJSON* item = cJSON_GetObjectItemCaseSensitive(pcfg, "enabled");
            if (item && cJSON_IsBool(item)) {
                enabled = cJSON_IsTrue(item);
            }
        }

        if (enabled) {
            TE_PluginLoaderEnable(plugin);
        }
    }

    TE_LogWrite(TE_LOG_INFO, "Core Manager initialization complete with %u plugins loaded", g_core_state->plugin_count);
    return S_OK;
}

void TE_CoreManagerShutdown(void)
{
    if (!g_core_state) return;

    TE_LogWrite(TE_LOG_INFO, "Core Manager shutting down...");

    TE_ConfigWatcherStop();

    if (g_core_state->taskbar_hwnd) {
        TE_TaskbarSubclassRemove(g_core_state->taskbar_hwnd);
    }

    TE_PluginLoaderUnloadAll(g_core_state->plugins, g_core_state->plugin_count);

    if (g_core_state->config_root) {
        cJSON_Delete(g_core_state->config_root);
        g_core_state->config_root = NULL;
    }

    TE_LogShutdown();

    HeapFree(GetProcessHeap(), 0, g_core_state);
    g_core_state = NULL;
}

void TE_CoreManagerOnConfigChanged(void* core_state_ptr)
{
    TE_CoreState* state = (TE_CoreState*)core_state_ptr;
    if (!state) return;

    TE_LogWrite(TE_LOG_INFO, "Core Manager processing config hot-reload...");

    cJSON* new_root = NULL;
    HRESULT hr = TE_ConfigLoad(state->config_path, &new_root);
    if (FAILED(hr) || !new_root) {
        TE_LogWrite(TE_LOG_ERROR, "Config hot-reload failed to parse new config file");
        return;
    }

    /* Diff per plugin */
    for (uint32_t i = 0; i < state->plugin_count; i++) {
        TE_PluginEntry* plugin = &state->plugins[i];
        const char* name = plugin->metadata->name;

        const cJSON* old_sec = TE_ConfigGetPluginSection(state->config_root, name);
        const cJSON* new_sec = TE_ConfigGetPluginSection(new_root, name);

        char* old_str = old_sec ? cJSON_PrintUnformatted(old_sec) : NULL;
        char* new_str = new_sec ? cJSON_PrintUnformatted(new_sec) : NULL;

        bool changed = false;
        if (!old_str && new_str) changed = true;
        else if (old_str && !new_str) changed = true;
        else if (old_str && new_str && strcmp(old_str, new_str) != 0) changed = true;

        if (old_str) cJSON_free(old_str);
        if (new_str) cJSON_free(new_str);

        if (changed) {
            TE_LogWrite(TE_LOG_INFO, "Config section for plugin '%s' changed", name);
            if (plugin->context) {
                plugin->context->config = new_sec;
            }

            /* Dispatch CONFIG_CHANGED */
            TE_ConfigChangedEvent evt = { .new_config = new_sec };
            TE_EventDispatch(state->subscriptions, state->subscription_count, TE_EVENT_CONFIG_CHANGED, &evt);
        }
    }

    if (state->config_root) {
        cJSON_Delete(state->config_root);
    }
    state->config_root = new_root;
    TE_LogWrite(TE_LOG_INFO, "Config hot-reload complete");
}
