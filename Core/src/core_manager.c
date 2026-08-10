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
#include "core/state_store.h"
#include <sdk/te_debug_trace.h>
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

static HRESULT CorePublishState(const char* key, const StateValue* val)
{
    return TE_StatePublish(key, val);
}

static HRESULT CoreQueryState(const char* key, StateValue* out_val)
{
    return TE_StateQuery(key, out_val);
}

static bool IsPluginEnabledInConfig(const cJSON* config)
{
    const cJSON* item = config ? cJSON_GetObjectItemCaseSensitive(config, "enabled") : NULL;
    return !item || !cJSON_IsBool(item) || cJSON_IsTrue(item);
}

HRESULT TE_CoreManagerInitPhaseA(HINSTANCE hinstance)
{
    TE_DebugTrace("[TE-DBG] PhaseA: Entering TE_CoreManagerInitPhaseA\n");
    if (g_core_state) {
        TE_DebugTrace("[TE-DBG] PhaseA: Already initialized\n");
        return S_OK;
    }

    HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
    TE_DebugTraceFmt("[TE-DBG] PhaseA: FindWindowW returned HWND=0x%p\n", (void*)taskbar_hwnd);
    if (!taskbar_hwnd) return E_PENDING;

    DWORD taskbar_tid = GetWindowThreadProcessId(taskbar_hwnd, NULL);
    if (GetCurrentThreadId() != taskbar_tid) {
        TE_DebugTrace("[TE-DBG] PhaseA: Wrong thread, returning E_PENDING\n");
        /* SetWindowSubclass must be called from the thread that owns the window */
        return E_PENDING;
    }

    g_core_state = (TE_CoreState*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TE_CoreState));
    if (!g_core_state) return E_OUTOFMEMORY;
    TE_DebugTrace("[TE-DBG] PhaseA: g_core_state allocated\n");

    g_core_state->hinstance = hinstance;
    g_core_state->taskbar_hwnd = taskbar_hwnd;

    /* Initialize Event Dispatch Table */
    TE_EventDispatchInit(g_core_state->subscriptions, &g_core_state->subscription_count);

    /* Install Taskbar Subclass */
    HRESULT sub_hr = TE_TaskbarSubclassInstall(g_core_state->taskbar_hwnd, g_core_state->subscriptions,
                                              &g_core_state->subscription_count, g_core_state);
    TE_DebugTraceFmt("[TE-DBG] PhaseA: SubclassInstall returned hr=0x%08X\n", (unsigned int)sub_hr);
    if (SUCCEEDED(sub_hr)) {
        PostMessageW(g_core_state->taskbar_hwnd, WM_APP + 100 /* WM_TE_INIT */, 0, 0);
        TE_DebugTrace("[TE-DBG] PhaseA: Posted WM_TE_INIT to taskbar\n");
    } else {
        HeapFree(GetProcessHeap(), 0, g_core_state);
        g_core_state = NULL;
        return E_FAIL;
    }

    return S_OK;
}

HRESULT TE_CoreManagerInitPhaseB(void)
{
    TE_DebugTrace("[TE-DBG] PhaseB: Entering TE_CoreManagerInitPhaseB\n");
    if (!g_core_state) return E_POINTER;

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
        TE_LogInit(log_dir, TE_LOG_DEBUG, true);
    } else {
        TE_LogInit(NULL, TE_LOG_DEBUG, true);
    }

    TE_LogWrite(TE_LOG_INFO, "Core Manager initializing Phase B...");

    /* Start other event sources now that we are outside CBT hook.
     * Keep RegisterShellHookWindow disabled for now: even with a helper HWND,
     * registering it from inside Explorer produced delayed Shell_TrayWnd loss
     * without a crash on Win11 XAML taskbar builds. */
    if (g_core_state->taskbar_hwnd) {
        TE_DebugTrace("[TE-DBG] PhaseB: ShellHookStart skipped for taskbar stability\n");
        TE_PowerDeviceStart(g_core_state->taskbar_hwnd, g_core_state->subscriptions, &g_core_state->subscription_count);
        TE_DebugTrace("[TE-DBG] PhaseB: PowerDeviceStart completed\n");
        TE_VDesktopNotifyStart(g_core_state->subscriptions, &g_core_state->subscription_count);
        TE_DebugTrace("[TE-DBG] PhaseB: VDesktopNotifyStart completed\n");
    }

    /* Load Configuration */
    HRESULT hr = TE_ConfigLoad(g_core_state->config_path, &g_core_state->config_root);
    TE_DebugTraceFmt("[TE-DBG] PhaseB: ConfigLoad returned hr=0x%08X\n", (unsigned int)hr);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_WARN, "Failed to load config, starting with empty config");
    }

    /* Resolve Modules Directory */
    HINSTANCE mod_inst = g_core_state->hinstance ? g_core_state->hinstance : GetModuleHandleW(NULL);
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

    /* Start Config Directory Watcher */
    wchar_t config_dir[MAX_PATH];
    wcsncpy(config_dir, g_core_state->config_path, MAX_PATH - 1);
    config_dir[MAX_PATH - 1] = L'\0';
    wchar_t* cfg_dir_slash = wcsrchr(config_dir, L'\\');
    if (cfg_dir_slash) {
        *cfg_dir_slash = L'\0';
    } else {
        wcscpy_s(config_dir, MAX_PATH, L".");
    }
    HRESULT watch_hr = TE_ConfigWatcherStart(config_dir, g_core_state->taskbar_hwnd);
    if (FAILED(watch_hr)) {
        TE_LogWrite(TE_LOG_WARN, "Failed to start config watcher (hr: 0x%08X)", (unsigned int)watch_hr);
    }

    /* Discover & Load Plugins */
    TE_PluginLoaderScan(g_core_state->modules_dir, g_core_state->plugins, &g_core_state->plugin_count);
    TE_DebugTraceFmt("[TE-DBG] PhaseB: PluginLoaderScan found %u plugins\n", g_core_state->plugin_count);

    /* Initialize and Enable Plugins */
    for (uint32_t i = 0; i < g_core_state->plugin_count; i++) {
        TE_PluginEntry* plugin = &g_core_state->plugins[i];
        TE_DebugTraceFmt("[TE-DBG] PhaseB: Processing plugin[%u] name='%s'\n", i, (plugin->metadata && plugin->metadata->name) ? plugin->metadata->name : "NULL");
        if (!plugin->context) continue;

        g_core_state->current_plugin_id = i + 1;

        plugin->context->api_version = TE_API_VERSION;
        plugin->context->taskbar_hwnd = g_core_state->taskbar_hwnd;
        plugin->context->monitor = g_core_state->taskbar_hwnd ? MonitorFromWindow(g_core_state->taskbar_hwnd, MONITOR_DEFAULTTONEAREST) : NULL;
        plugin->context->dpi = g_core_state->taskbar_hwnd ? GetDpiForWindow(g_core_state->taskbar_hwnd) : 96;
        if (plugin->context->dpi == 0) plugin->context->dpi = 96;
        plugin->context->config = TE_ConfigGetPluginSection(g_core_state->config_root, plugin->metadata->name);
        plugin->context->log = TE_LogWrite;
        plugin->context->subscribe = CoreSubscribeWrapper;
        plugin->context->unsubscribe = CoreUnsubscribeWrapper;
        plugin->context->request_redraw = CoreRequestRedrawNoop;
        plugin->context->publish_state = CorePublishState;
        plugin->context->query_state = CoreQueryState;
        plugin->context->core_opaque = (void*)(uintptr_t)i;

        if (plugin->iface->Initialize) {
            TE_DebugTraceFmt("[TE-DBG] PhaseB: Calling Initialize for '%s'\n", plugin->metadata->name);
            TE_FaultIsolationCallPluginInit(plugin, plugin->iface->Initialize, plugin->context);
            TE_DebugTraceFmt("[TE-DBG] PhaseB: Initialize returned for '%s'\n", plugin->metadata->name);
        }

        /* Check enabled setting in config */
        if (IsPluginEnabledInConfig(plugin->context->config)) {
            TE_DebugTraceFmt("[TE-DBG] PhaseB: Enabling plugin '%s'\n", plugin->metadata->name);
            TE_PluginLoaderEnable(plugin);
            TE_DebugTraceFmt("[TE-DBG] PhaseB: Plugin '%s' Enable returned\n", plugin->metadata->name);
        }
    }

    g_core_state->current_plugin_id = 0;

    HRESULT ipc_hr = TE_IpcServerStart();
    TE_DebugTraceFmt("[TE-DBG] PhaseB: IpcServerStart returned hr=0x%08X\n", (unsigned int)ipc_hr);
    if (FAILED(ipc_hr)) {
        TE_LogWrite(TE_LOG_WARN, "Failed to start IPC server (hr: 0x%08X)", (unsigned int)ipc_hr);
    }

    TE_LogWrite(TE_LOG_INFO, "Core Manager initialization Phase B complete with %u plugins loaded", g_core_state->plugin_count);
    TE_DebugTrace("[TE-DBG] PhaseB: COMPLETE - All initialization done\n");
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
        if (!plugin->metadata || !plugin->metadata->name) continue;
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

        /* Every context points into the active configuration tree. Refresh even
         * unchanged sections before the previous tree is released below. */
        if (plugin->context) {
            plugin->context->config = new_sec;
        }

        const bool should_enable = IsPluginEnabledInConfig(new_sec);
        if (should_enable && !plugin->enabled) {
            TE_PluginLoaderEnable(plugin);
        } else if (!should_enable && plugin->enabled) {
            TE_PluginLoaderDisable(plugin);
        }

        if (changed && plugin->enabled) {
            TE_LogWrite(TE_LOG_INFO, "Config section for plugin '%s' changed", name);
            /* Dispatch CONFIG_CHANGED targeted specifically to this plugin (plugin_id = i + 1) */
            TE_ConfigChangedEvent evt = { .new_config = new_sec };
            TE_EventDispatchTargeted(state->subscriptions, state->subscription_count,
                                     TE_EVENT_CONFIG_CHANGED, &evt, i + 1);
        }
    }

    /* Replace old root */
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
    if (!g_core_state) return E_POINTER;

    for (uint32_t i = 0; i < g_core_state->plugin_count; i++) {
        TE_PluginEntry* plugin = &g_core_state->plugins[i];
        if (plugin->metadata && plugin->metadata->name && strcmp(plugin->metadata->name, plugin_name) == 0) {
            HRESULT hr = enabled ? TE_PluginLoaderEnable(plugin) : TE_PluginLoaderDisable(plugin);
            if (SUCCEEDED(hr) && enabled && plugin->enabled && plugin->context) {
                TE_ConfigChangedEvent evt = { .new_config = plugin->context->config };
                TE_EventDispatchTargeted(g_core_state->subscriptions, g_core_state->subscription_count,
                                         TE_EVENT_CONFIG_CHANGED, &evt, i + 1);
            }
            return hr;
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

uint32_t TE_CoreManagerBuildSettingsSchema(char* buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) return 0;
    buffer[0] = '\0';
    if (!g_core_state) return 0;

    cJSON* root = cJSON_CreateObject();
    cJSON* plugins_array = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "plugins", plugins_array);

    for (uint32_t i = 0; i < g_core_state->plugin_count; i++) {
        TE_PluginEntry* plugin = &g_core_state->plugins[i];
        if (!plugin->iface || !plugin->iface->GetMetadata || !plugin->iface->GetSettings) continue;

        const PluginMetadata* meta = plugin->iface->GetMetadata();
        const PluginSettings* settings = plugin->iface->GetSettings();
        if (!meta || !settings) continue;

        cJSON* plugin_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(plugin_obj, "name", meta->name ? meta->name : "unknown");
        cJSON_AddStringToObject(plugin_obj, "version", meta->version ? meta->version : "0.0.0");
        cJSON_AddStringToObject(plugin_obj, "description", meta->description ? meta->description : "");

        cJSON* settings_array = cJSON_CreateArray();
        cJSON_AddItemToObject(plugin_obj, "settings", settings_array);

        for (size_t j = 0; j < settings->count; j++) {
            const SettingDescriptor* desc = &settings->descriptors[j];
            cJSON* setting_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(setting_obj, "key", desc->key ? desc->key : "");
            cJSON_AddStringToObject(setting_obj, "label", desc->label ? desc->label : "");
            cJSON_AddStringToObject(setting_obj, "tooltip", desc->tooltip ? desc->tooltip : "");
            
            switch (desc->type) {
                case TE_SETTING_BOOL:
                    cJSON_AddStringToObject(setting_obj, "type", "bool");
                    cJSON_AddBoolToObject(setting_obj, "default", desc->value.b.default_val);
                    break;
                case TE_SETTING_INT:
                    cJSON_AddStringToObject(setting_obj, "type", "int");
                    cJSON_AddNumberToObject(setting_obj, "default", desc->value.i.default_val);
                    cJSON_AddNumberToObject(setting_obj, "min", desc->value.i.min);
                    cJSON_AddNumberToObject(setting_obj, "max", desc->value.i.max);
                    cJSON_AddNumberToObject(setting_obj, "step", desc->value.i.step);
                    break;
                case TE_SETTING_FLOAT:
                    cJSON_AddStringToObject(setting_obj, "type", "float");
                    cJSON_AddNumberToObject(setting_obj, "default", desc->value.f.default_val);
                    cJSON_AddNumberToObject(setting_obj, "min", desc->value.f.min);
                    cJSON_AddNumberToObject(setting_obj, "max", desc->value.f.max);
                    cJSON_AddNumberToObject(setting_obj, "step", desc->value.f.step);
                    break;
                case TE_SETTING_STRING:
                    cJSON_AddStringToObject(setting_obj, "type", "string");
                    cJSON_AddStringToObject(setting_obj, "default", desc->value.s.default_val ? desc->value.s.default_val : "");
                    break;
                case TE_SETTING_ENUM: {
                    cJSON_AddStringToObject(setting_obj, "type", "enum");
                    cJSON_AddStringToObject(setting_obj, "default", desc->value.e.default_val ? desc->value.e.default_val : "");
                    cJSON* options_array = cJSON_CreateArray();
                    for (int k = 0; k < desc->value.e.count; k++) {
                        cJSON_AddItemToArray(options_array, cJSON_CreateString(desc->value.e.options[k]));
                    }
                    cJSON_AddItemToObject(setting_obj, "options", options_array);
                    break;
                }
                case TE_SETTING_COLOR:
                    cJSON_AddStringToObject(setting_obj, "type", "color");
                    cJSON_AddNumberToObject(setting_obj, "default", desc->value.color.default_val);
                    break;
            }
            cJSON_AddItemToArray(settings_array, setting_obj);
        }
        cJSON_AddItemToArray(plugins_array, plugin_obj);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    uint32_t len = 0;
    if (json_str) {
        size_t str_len = strlen(json_str);
        if (str_len < buffer_len) {
            strcpy(buffer, json_str);
            len = (uint32_t)(str_len + 1);
        }
        cJSON_free(json_str);
    }
    return len;
}

uint32_t TE_CoreManagerBuildPerfStats(char* buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) return 0;
    buffer[0] = '\0';
    
    StateValue fps, avg_ms, min_ms, max_ms;
    
    float f_fps = 0.0f;
    float f_avg_ms = 0.0f;
    float f_min_ms = 0.0f;
    float f_max_ms = 0.0f;
    
    if (SUCCEEDED(TE_StateQuery("perf.fps", &fps)) && fps.type == TE_STATE_TYPE_FLOAT) {
        f_fps = fps.value.f;
    }
    if (SUCCEEDED(TE_StateQuery("perf.avg_ms", &avg_ms)) && avg_ms.type == TE_STATE_TYPE_FLOAT) {
        f_avg_ms = avg_ms.value.f;
    }
    if (SUCCEEDED(TE_StateQuery("perf.min_ms", &min_ms)) && min_ms.type == TE_STATE_TYPE_FLOAT) {
        f_min_ms = min_ms.value.f;
    }
    if (SUCCEEDED(TE_StateQuery("perf.max_ms", &max_ms)) && max_ms.type == TE_STATE_TYPE_FLOAT) {
        f_max_ms = max_ms.value.f;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "fps", f_fps);
    cJSON_AddNumberToObject(root, "avg_ms", f_avg_ms);
    cJSON_AddNumberToObject(root, "min_ms", f_min_ms);
    cJSON_AddNumberToObject(root, "max_ms", f_max_ms);

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    uint32_t len = 0;
    if (json_str) {
        size_t str_len = strlen(json_str);
        if (str_len < buffer_len) {
            strcpy(buffer, json_str);
            len = (uint32_t)(str_len + 1);
        }
        cJSON_free(json_str);
    }
    return len;
}
