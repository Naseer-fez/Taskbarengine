#include "core/core_manager.h"
#include "core/config.h"
#include "core/config_watcher.h"
#include "core/event_dispatch.h"
#include "core/plugin_loader.h"
#include "core/fault_isolation.h"
#include "core/taskbar_subclass.h"
#include "core/ipc_server.h"
#include "core/shell_hook.h"
#include "core/power_device.h"
#include "core/vdesktop_notify.h"
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
    uint32_t       current_plugin_id;
};

static TE_CoreState* g_core_state = NULL;

static HRESULT CoreSubscribeWrapper(uint32_t event_type, EventCallbackFunc callback, void* user_data)
{
    if (!g_core_state) return E_POINTER;
    uint32_t plugin_id = g_core_state->current_plugin_id;
    TE_PluginEntry* entry = (plugin_id > 0 && plugin_id <= g_core_state->plugin_count) ? &g_core_state->plugins[plugin_id - 1] : NULL;
    return TE_EventSubscribeEx(g_core_state->subscriptions, &g_core_state->subscription_count,
                               (TE_EventType)event_type, callback, user_data, plugin_id, entry);
}

static HRESULT CoreUnsubscribeWrapper(uint32_t event_type, EventCallbackFunc callback)
{
    if (!g_core_state) return E_POINTER;
    return TE_EventUnsubscribe(g_core_state->subscriptions, &g_core_state->subscription_count,
                               (TE_EventType)event_type, callback);
}

static void CoreRequestRedrawNoop(void) {}

#include "core/state_store.h"

static HRESULT CorePublishState(const char* key, const StateValue* val)
{
    return TE_StatePublish(key, val);
}

static HRESULT CoreQueryState(const char* key, StateValue* out_val)
{
    return TE_StateQuery(key, out_val);
}

HRESULT TE_CoreManagerInit(HINSTANCE hinstance)
{
    if (g_core_state) return S_OK;

    g_core_state = (TE_CoreState*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TE_CoreState));
    if (!g_core_state) return E_OUTOFMEMORY;

    g_core_state->hinstance = hinstance;
    g_core_state->taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);

    HRESULT path_hr = TE_ConfigResolvePath(g_core_state->config_path, MAX_PATH);
    if (FAILED(path_hr)) {
        wcsncpy(g_core_state->config_path, L"config.jsonc", MAX_PATH - 1);
    }

    /* Initialize State Store */
    TE_StateStoreInit();

    /* Initialize Logging */
    wchar_t log_dir[MAX_PATH];
    wcsncpy(log_dir, g_core_state->config_path, MAX_PATH - 1);
    log_dir[MAX_PATH - 1] = L'\0';
    wchar_t* last_slash = wcsrchr(log_dir, L'\\');
    if (last_slash) {
        wcsncpy(last_slash + 1, L"logs", MAX_PATH - (last_slash + 1 - log_dir) - 1);
    } else {
        wcsncpy(log_dir, L"logs", MAX_PATH - 1);
    }
    TE_LogInit(log_dir, TE_LOG_DEBUG, true);

    TE_LogWrite(TE_LOG_INFO, "Core Manager initializing...");

    /* Load Configuration */
    HRESULT hr = TE_ConfigLoad(g_core_state->config_path, &g_core_state->config_root);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_WARN, "Failed to load config, starting with empty config");
    }

    /* Resolve Modules Directory */
    HINSTANCE mod_inst = hinstance ? hinstance : GetModuleHandleW(NULL);
    wchar_t dll_path[MAX_PATH];
    DWORD len = GetModuleFileNameW(mod_inst, dll_path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        wchar_t* slash = wcsrchr(dll_path, L'\\');
        if (slash) {
            *slash = L'\0';
            swprintf(g_core_state->modules_dir, MAX_PATH, L"%s\\Modules", dll_path);
        }
    }
    if (g_core_state->modules_dir[0] == L'\0') {
        DWORD proc_len = GetModuleFileNameW(NULL, dll_path, MAX_PATH);
        if (proc_len > 0 && proc_len < MAX_PATH) {
            wchar_t* slash = wcsrchr(dll_path, L'\\');
            if (slash) {
                *slash = L'\0';
                swprintf(g_core_state->modules_dir, MAX_PATH, L"%s\\Modules", dll_path);
            }
        }
    }

    /* Initialize Event Dispatch Table */
    TE_EventDispatchInit(g_core_state->subscriptions, &g_core_state->subscription_count);

    /* Install Taskbar Subclass */
    if (g_core_state->taskbar_hwnd) {
        HRESULT sub_hr = TE_TaskbarSubclassInstall(g_core_state->taskbar_hwnd, g_core_state->subscriptions,
                                                  &g_core_state->subscription_count, g_core_state);
        if (FAILED(sub_hr)) {
            TE_LogWrite(TE_LOG_WARN, "Failed to subclass Shell_TrayWnd (hr: 0x%08X)", (unsigned int)sub_hr);
        }
        TE_ShellHookStart(g_core_state->taskbar_hwnd, g_core_state->subscriptions, &g_core_state->subscription_count);
        TE_PowerDeviceStart(g_core_state->taskbar_hwnd, g_core_state->subscriptions, &g_core_state->subscription_count);
        TE_VDesktopNotifyStart(g_core_state->subscriptions, &g_core_state->subscription_count);
    }

    /* Start Config Directory Watcher */
    wchar_t config_dir[MAX_PATH];
    wcsncpy(config_dir, g_core_state->config_path, MAX_PATH - 1);
    config_dir[MAX_PATH - 1] = L'\0';
    last_slash = wcsrchr(config_dir, L'\\');
    if (last_slash) *last_slash = L'\0';
    HRESULT watch_hr = TE_ConfigWatcherStart(config_dir, g_core_state->taskbar_hwnd);
    if (FAILED(watch_hr)) {
        TE_LogWrite(TE_LOG_WARN, "Failed to start config watcher (hr: 0x%08X)", (unsigned int)watch_hr);
    }

    /* Discover & Load Plugins */
    TE_PluginLoaderScan(g_core_state->modules_dir, g_core_state->plugins, &g_core_state->plugin_count);

    /* Initialize and Enable Plugins */
    for (uint32_t i = 0; i < g_core_state->plugin_count; i++) {
        TE_PluginEntry* plugin = &g_core_state->plugins[i];
        if (!plugin->context) continue;

        g_core_state->current_plugin_id = i + 1;

        plugin->context->api_version = TE_API_VERSION;
        plugin->context->taskbar_hwnd = g_core_state->taskbar_hwnd;
        plugin->context->monitor = g_core_state->taskbar_hwnd ? MonitorFromWindow(g_core_state->taskbar_hwnd, MONITOR_DEFAULTTONEAREST) : NULL;
        plugin->context->dpi = 96; /* Default DPI */
        plugin->context->config = TE_ConfigGetPluginSection(g_core_state->config_root, plugin->metadata->name);
        plugin->context->log = TE_LogWrite;
        plugin->context->subscribe = CoreSubscribeWrapper;
        plugin->context->unsubscribe = CoreUnsubscribeWrapper;
        plugin->context->request_redraw = CoreRequestRedrawNoop;
        plugin->context->publish_state = CorePublishState;
        plugin->context->query_state = CoreQueryState;
        plugin->context->core_opaque = (void*)(uintptr_t)i;

        if (plugin->iface->Initialize) {
            TE_FaultIsolationCallPluginInit(plugin, plugin->iface->Initialize, plugin->context);
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

    g_core_state->current_plugin_id = 0;

    HRESULT ipc_hr = TE_IpcServerStart();
    if (FAILED(ipc_hr)) {
        TE_LogWrite(TE_LOG_WARN, "Failed to start IPC server (hr: 0x%08X)", (unsigned int)ipc_hr);
    }

    TE_LogWrite(TE_LOG_INFO, "Core Manager initialization complete with %u plugins loaded", g_core_state->plugin_count);
    return S_OK;
}

static void CoreManagerShutdownInternal(bool stop_ipc_server)
{
    if (!g_core_state) return;

    TE_LogWrite(TE_LOG_INFO, "Core Manager shutting down...");

    if (stop_ipc_server) {
        TE_IpcServerStop();
    }

    TE_ConfigWatcherStop();

    if (g_core_state->taskbar_hwnd) {
        TE_ShellHookStop(g_core_state->taskbar_hwnd);
        TE_TaskbarSubclassRemove(g_core_state->taskbar_hwnd);
    }
    TE_PowerDeviceStop();
    TE_VDesktopNotifyStop();

    TE_PluginLoaderUnloadAll(g_core_state->plugins, g_core_state->plugin_count);

    if (g_core_state->config_root) {
        cJSON_Delete(g_core_state->config_root);
        g_core_state->config_root = NULL;
    }

    TE_StateStoreShutdown();

    TE_LogShutdown();

    HeapFree(GetProcessHeap(), 0, g_core_state);
    g_core_state = NULL;
}

void TE_CoreManagerShutdown(void)
{
    CoreManagerShutdownInternal(true);
}

void TE_CoreManagerShutdownFromIpc(void)
{
    CoreManagerShutdownInternal(false);
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

            /* Dispatch CONFIG_CHANGED targeted specifically to this plugin (plugin_id = i + 1) */
            TE_ConfigChangedEvent evt = { .new_config = new_sec };
            TE_EventDispatchTargeted(state->subscriptions, state->subscription_count, TE_EVENT_CONFIG_CHANGED, &evt, i + 1);
        }
    }

    if (state->config_root) {
        cJSON_Delete(state->config_root);
    }
    state->config_root = new_root;
    TE_LogWrite(TE_LOG_INFO, "Config hot-reload complete");
}

void TE_CoreManagerReloadConfig(void)
{
    if (g_core_state) {
        TE_CoreManagerOnConfigChanged(g_core_state);
    }
}

HRESULT TE_CoreManagerSetPluginEnabledByName(const char* plugin_name, bool enabled)
{
    if (!g_core_state || !plugin_name) return E_POINTER;

    for (uint32_t i = 0; i < g_core_state->plugin_count; i++) {
        TE_PluginEntry* plugin = &g_core_state->plugins[i];
        if (plugin->metadata && plugin->metadata->name && strcmp(plugin->metadata->name, plugin_name) == 0) {
            return enabled ? TE_PluginLoaderEnable(plugin) : TE_PluginLoaderDisable(plugin);
        }
    }

    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

uint32_t TE_CoreManagerBuildPluginList(char* buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) return 0;
    buffer[0] = '\0';
    if (!g_core_state) return 0;

    size_t used = 0;
    for (uint32_t i = 0; i < g_core_state->plugin_count && used < buffer_len; i++) {
        TE_PluginEntry* plugin = &g_core_state->plugins[i];
        const char* name = (plugin->metadata && plugin->metadata->name) ? plugin->metadata->name : "unknown";
        int wrote = snprintf(buffer + used, buffer_len - used, "%s\t%s\n", name, plugin->enabled ? "enabled" : "disabled");
        if (wrote < 0) break;
        if ((size_t)wrote >= buffer_len - used) {
            used = buffer_len - 1;
            buffer[used] = '\0';
            break;
        }
        used += (size_t)wrote;
    }

    return (uint32_t)(used + 1);
}

uint32_t TE_CoreManagerGetCurrentPluginId(void)
{
    return g_core_state ? g_core_state->current_plugin_id : 0;
}

void TE_CoreManagerSetCurrentPluginId(uint32_t plugin_id)
{
    if (g_core_state) {
        g_core_state->current_plugin_id = plugin_id;
    }
}
