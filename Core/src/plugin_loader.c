#include "core/plugin_loader.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "core/event_dispatch.h"
#include "core/fault_isolation.h"
#include "core/config.h"
#include "core/state_store.h"
#include <sdk/te_log.h>
#include <sdk/te_events.h>
#include <cJSON.h>

static TE_PluginEntry g_plugins[TE_MAX_PLUGINS];
static int g_plugin_count = 0;
static uint32_t g_current_plugin_id = 0;
static TE_PluginEntry* g_current_plugin_entry = NULL;
static SRWLOCK g_plugin_lock = SRWLOCK_INIT;

typedef struct TE_InterfaceProbe {
    GetPluginInterfaceFunc function;
    const PluginInterface* result;
} TE_InterfaceProbe;

typedef struct TE_MetadataProbe {
    const PluginInterface* interface_ptr;
    const PluginMetadata* result;
} TE_MetadataProbe;

static HRESULT TE_CallInterfaceFactory(void* context) {
    TE_InterfaceProbe* probe = (TE_InterfaceProbe*)context;
    probe->result = probe->function();
    return probe->result ? TE_S_OK : TE_E_FAIL;
}

static HRESULT TE_CallGetMetadata(void* context) {
    TE_MetadataProbe* probe = (TE_MetadataProbe*)context;
    probe->result = probe->interface_ptr->GetMetadata();
    return probe->result ? TE_S_OK : TE_E_FAIL;
}

static HRESULT TE_PluginSubscribeWrapper(uint32_t event_type, void (*callback)(uint32_t, const void*, void*), void* user_data) {
    return TE_EventDispatchSubscribe(event_type, (TE_EventCallback)callback, user_data, g_current_plugin_id);
}

static HRESULT TE_PluginUnsubscribeWrapper(uint32_t event_type, void (*callback)(uint32_t, const void*, void*)) {
    return TE_EventDispatchUnsubscribe(event_type, (TE_EventCallback)callback);
}

#include "core/taskbar_subclass.h"

static HRESULT TE_PluginSubscribeMessageWrapper(UINT msg) {
    return TE_TaskbarSubclassSubscribeMessage(msg);
}

static HRESULT TE_PluginUnsubscribeMessageWrapper(UINT msg) {
    return TE_TaskbarSubclassUnsubscribeMessage(msg);
}

static HRESULT TE_PluginRegisterTimerWrapper(uint32_t interval_ms, void (*callback)(void*), void* user_data, uint32_t* out_timer_id) {
    (void)interval_ms; (void)callback; (void)user_data; (void)out_timer_id;
    /* TODO(Phase3): Implement timer registration */
    return TE_E_NOTIMPL;
}

static HRESULT TE_PluginCancelTimerWrapper(uint32_t timer_id) {
    (void)timer_id;
    return TE_E_NOTIMPL;
}

static void TE_PluginRequestRedrawWrapper(void) {
    /* TODO(Phase3): Invalidate taskbar rect */
}

HRESULT TE_PluginLoaderInit(void) {
    HRESULT shutdown_hr = TE_PluginLoaderShutdown();
    if (FAILED(shutdown_hr)) return shutdown_hr;

    InitializeSRWLock(&g_plugin_lock);
    AcquireSRWLockExclusive(&g_plugin_lock);
    g_plugin_count = 0;
    g_current_plugin_id = 0;
    g_current_plugin_entry = NULL;
    memset(g_plugins, 0, sizeof(g_plugins));
    ReleaseSRWLockExclusive(&g_plugin_lock);
    return TE_S_OK;
}

HRESULT TE_PluginLoaderShutdown(void) {
    HRESULT hr = TE_PluginLoaderShutdownAll();
    if (FAILED(hr)) return hr;
    AcquireSRWLockExclusive(&g_plugin_lock);
    g_plugin_count = 0;
    g_current_plugin_id = 0;
    g_current_plugin_entry = NULL;
    ReleaseSRWLockExclusive(&g_plugin_lock);
    return TE_S_OK;
}

static void TryLoadPluginDll(const wchar_t* dll_path) {
    if (g_plugin_count >= TE_MAX_PLUGINS) return;
    for (int i = 0; i < g_plugin_count; i++) {
        if (_wcsicmp(g_plugins[i].dll_path, dll_path) == 0) return;
    }
    HMODULE hMod = LoadLibraryW(dll_path);
    if (hMod) {
        BOOL loaded = FALSE;
        GetPluginInterfaceFunc get_interface = (GetPluginInterfaceFunc)(void*)GetProcAddress(hMod, "GetPluginInterface");
        if (get_interface) {
            int probe_faults = 0;
            TE_InterfaceProbe interface_probe = { get_interface, NULL };
            HRESULT probe_hr = TE_FaultIsolatedCallWithContext(&probe_faults, "plugin-candidate",
                                                               "GetPluginInterface",
                                                               TE_CallInterfaceFactory, &interface_probe);
            const PluginInterface* iface = SUCCEEDED(probe_hr) ? interface_probe.result : NULL;
            if (iface) {
                TE_MetadataProbe metadata_probe = { iface, NULL };
                probe_hr = TE_FaultIsolatedCallWithContext(&probe_faults, "plugin-candidate",
                                                           "GetMetadata",
                                                           TE_CallGetMetadata, &metadata_probe);
                const PluginMetadata* meta = SUCCEEDED(probe_hr) ? metadata_probe.result : NULL;
                if (meta && meta->name && meta->name[0] != '\0' && meta->api_version <= TE_API_VERSION) {
                    BOOL duplicate = FALSE;
                    for (int i = 0; i < g_plugin_count; i++) {
                        if (g_plugins[i].metadata && g_plugins[i].metadata->name &&
                            strcmp(g_plugins[i].metadata->name, meta->name) == 0) {
                            duplicate = TRUE;
                            break;
                        }
                    }
                    if (!duplicate) {
                        g_plugins[g_plugin_count].module_handle = hMod;
                        g_plugins[g_plugin_count].interface_ptr = iface;
                        g_plugins[g_plugin_count].metadata = meta;
                        g_plugins[g_plugin_count].plugin_id = (uint32_t)g_plugin_count + 1;
                        wcsncpy_s(g_plugins[g_plugin_count].dll_path, MAX_PATH, dll_path, _TRUNCATE);
                        {
                            char message[MAX_PATH * 4 + 128];
                            char path_utf8[MAX_PATH * 4] = { 0 };
                            WideCharToMultiByte(CP_UTF8, 0, dll_path, -1, path_utf8,
                                                (int)sizeof(path_utf8), NULL, NULL);
                            snprintf(message, sizeof(message), "Loaded plugin '%s' (priority=%u, path=%s)",
                                     meta->name, (unsigned)meta->priority, path_utf8);
                            TE_LogWrite(TE_LOG_INFO, "PluginLoader", message);
                        }
                        g_plugin_count++;
                        loaded = TRUE;
                    }
                }
            }
        }
        if (!loaded) {
            char message[MAX_PATH * 4 + 128];
            char path_utf8[MAX_PATH * 4] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, dll_path, -1, path_utf8,
                                (int)sizeof(path_utf8), NULL, NULL);
            snprintf(message, sizeof(message), "Plugin DLL rejected (invalid interface or duplicate metadata): %s", path_utf8);
            TE_LogWrite(TE_LOG_WARNING, "PluginLoader", message);
            FreeLibrary(hMod);
        }
    } else {
        char message[MAX_PATH + 96];
        const wchar_t* name = wcsrchr(dll_path, L'\\');
        name = name ? name + 1 : dll_path;
        char narrow_name[MAX_PATH] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow_name, MAX_PATH, NULL, NULL);
        snprintf(message, sizeof(message), "Could not load plugin DLL '%s' (Win32 error %lu)", narrow_name, (unsigned long)GetLastError());
        TE_LogWrite(TE_LOG_WARNING, "PluginLoader", message);
    }
}

HRESULT TE_PluginLoaderScanAndLoad(const wchar_t* modules_dir) {
    if (!modules_dir || modules_dir[0] == L'\0') return TE_E_INVALIDARG;

    AcquireSRWLockExclusive(&g_plugin_lock);

    /* Prefer packaged per-plugin directories over stale flat compatibility copies. */
    WIN32_FIND_DATAW find_data;
    wchar_t search_path[MAX_PATH];
    swprintf(search_path, MAX_PATH, L"%s\\*", modules_dir);
    
    HANDLE hFind = FindFirstFileW(search_path, &find_data);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) continue;
                
                wchar_t dll_search[MAX_PATH];
                swprintf(dll_search, MAX_PATH, L"%s\\%s\\*.dll", modules_dir, find_data.cFileName);
                
                WIN32_FIND_DATAW dll_data;
                HANDLE hDllFind = FindFirstFileW(dll_search, &dll_data);
                if (hDllFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (_wcsicmp(dll_data.cFileName, L"EngineDLL.dll") == 0) continue;
                        wchar_t dll_path[MAX_PATH];
                        swprintf(dll_path, MAX_PATH, L"%s\\%s\\%s", modules_dir, find_data.cFileName, dll_data.cFileName);
                        TryLoadPluginDll(dll_path);
                    } while (FindNextFileW(hDllFind, &dll_data));
                    FindClose(hDllFind);
                }
            }
        } while (FindNextFileW(hFind, &find_data));
        FindClose(hFind);
    }

    /* Flat DLLs are legacy fallback candidates; nested packages win duplicates. */
    wchar_t flat_search[MAX_PATH];
    swprintf(flat_search, MAX_PATH, L"%s\\*.dll", modules_dir);
    WIN32_FIND_DATAW flat_data;
    HANDLE hFlatFind = FindFirstFileW(flat_search, &flat_data);
    if (hFlatFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(flat_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (_wcsicmp(flat_data.cFileName, L"EngineDLL.dll") == 0) continue;
                wchar_t dll_path[MAX_PATH];
                swprintf(dll_path, MAX_PATH, L"%s\\%s", modules_dir, flat_data.cFileName);
                TryLoadPluginDll(dll_path);
            }
        } while (FindNextFileW(hFlatFind, &flat_data));
        FindClose(hFlatFind);
    }
    
    for (int i = 1; i < g_plugin_count; i++) {
        TE_PluginEntry temp = g_plugins[i];
        int j = i - 1;
        while (j >= 0 && g_plugins[j].metadata->priority > temp.metadata->priority) {
            g_plugins[j + 1] = g_plugins[j];
            j--;
        }
        g_plugins[j + 1] = temp;
    }
    
    ReleaseSRWLockExclusive(&g_plugin_lock);
    return TE_S_OK;
}

static HRESULT PluginInitFunc(void) {
    if (g_current_plugin_entry && g_current_plugin_entry->interface_ptr && g_current_plugin_entry->interface_ptr->Initialize) {
        return g_current_plugin_entry->interface_ptr->Initialize(&g_current_plugin_entry->context);
    }
    return TE_S_OK;
}

HRESULT TE_PluginLoaderInitializeAll(HWND taskbar_hwnd, uint32_t dpi, const struct cJSON* config_root) {
    AcquireSRWLockExclusive(&g_plugin_lock);
    for (int i = 0; i < g_plugin_count; i++) {
        PluginContext* ctx = &g_plugins[i].context;
        memset(ctx, 0, sizeof(*ctx));
        ctx->struct_size = sizeof(PluginContext);
        ctx->api_version = TE_API_VERSION;
        ctx->taskbar_hwnd = taskbar_hwnd;
        ctx->monitor = MonitorFromWindow(taskbar_hwnd, MONITOR_DEFAULTTOPRIMARY);
        ctx->dpi = dpi;
        if (strcmp(g_plugins[i].metadata->name, "widget_host") == 0 && config_root) {
            ctx->config = cJSON_GetObjectItemCaseSensitive((cJSON*)config_root, "widgets");
        } else {
            ctx->config = TE_ConfigGetPluginSection((cJSON*)config_root, (char*)g_plugins[i].metadata->name);
        }
        ctx->log = TE_LogWrite;
        ctx->subscribe = TE_PluginSubscribeWrapper;
        ctx->unsubscribe = TE_PluginUnsubscribeWrapper;
        ctx->request_redraw = TE_PluginRequestRedrawWrapper;
        ctx->publish_state = (PublishStateFunc)TE_StatePublish;
        ctx->query_state = (QueryStateFunc)TE_StateQuery;
        ctx->core_opaque = &g_plugins[i];
        ctx->subscribe_message = TE_PluginSubscribeMessageWrapper;
        ctx->unsubscribe_message = TE_PluginUnsubscribeMessageWrapper;
        ctx->register_timer = TE_PluginRegisterTimerWrapper;
        ctx->cancel_timer = TE_PluginCancelTimerWrapper;
        
        g_current_plugin_id = g_plugins[i].plugin_id;
        g_current_plugin_entry = &g_plugins[i];
        
        HRESULT res = TE_FaultIsolatedCall(&g_plugins[i].fault_count, (char*)g_plugins[i].metadata->name, "Initialize", PluginInitFunc);
        if (SUCCEEDED(res)) {
            g_plugins[i].initialized = TRUE;
        }
    }
    ReleaseSRWLockExclusive(&g_plugin_lock);
    return TE_S_OK;
}

static HRESULT PluginEnableFunc(void) {
    if (g_current_plugin_entry && g_current_plugin_entry->interface_ptr && g_current_plugin_entry->interface_ptr->Enable) {
        return g_current_plugin_entry->interface_ptr->Enable();
    }
    return TE_S_OK;
}

HRESULT TE_PluginLoaderEnableAll(void) {
    AcquireSRWLockExclusive(&g_plugin_lock);
    for (int i = 0; i < g_plugin_count; i++) {
        if (g_plugins[i].initialized && !g_plugins[i].enabled) {
            g_current_plugin_id = g_plugins[i].plugin_id;
            g_current_plugin_entry = &g_plugins[i];
            HRESULT res = TE_FaultIsolatedCall(&g_plugins[i].fault_count, (char*)g_plugins[i].metadata->name, "Enable", PluginEnableFunc);
            if (SUCCEEDED(res)) {
                g_plugins[i].enabled = TRUE;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_plugin_lock);
    return TE_S_OK;
}

static HRESULT PluginDisableFunc(void) {
    if (g_current_plugin_entry && g_current_plugin_entry->interface_ptr && g_current_plugin_entry->interface_ptr->Disable) {
        return g_current_plugin_entry->interface_ptr->Disable();
    }
    return TE_S_OK;
}

static HRESULT PluginShutdownFunc(void) {
    if (g_current_plugin_entry && g_current_plugin_entry->interface_ptr && g_current_plugin_entry->interface_ptr->Shutdown) {
        return g_current_plugin_entry->interface_ptr->Shutdown();
    }
    return TE_S_OK;
}

void TE_PluginLoaderDisableAll(void) {
    AcquireSRWLockExclusive(&g_plugin_lock);
    for (int i = g_plugin_count - 1; i >= 0; i--) {
        if (g_plugins[i].enabled) {
            g_current_plugin_id = g_plugins[i].plugin_id;
            g_current_plugin_entry = &g_plugins[i];
            TE_FaultIsolatedCall(&g_plugins[i].fault_count, (char*)g_plugins[i].metadata->name, "Disable", PluginDisableFunc);
            g_plugins[i].enabled = FALSE;
        }
    }
    ReleaseSRWLockExclusive(&g_plugin_lock);
}

HRESULT TE_PluginLoaderShutdownAll(void) {
    HRESULT overall_hr = TE_S_OK;
    AcquireSRWLockExclusive(&g_plugin_lock);
    for (int i = 0; i < g_plugin_count; i++) {
        TE_EventDispatchRemoveByPlugin(g_plugins[i].plugin_id);
        if (g_plugins[i].initialized) {
            g_current_plugin_id = g_plugins[i].plugin_id;
            g_current_plugin_entry = &g_plugins[i];
            HRESULT hr = TE_FaultIsolatedCall(&g_plugins[i].fault_count,
                                              g_plugins[i].metadata ? (char*)g_plugins[i].metadata->name : "unknown",
                                              "Shutdown", PluginShutdownFunc);
            if (FAILED(hr)) {
                overall_hr = hr;
                continue; /* Keep the DLL loaded while plugin-owned work may still be running. */
            }
            g_plugins[i].initialized = FALSE;
        }
        if (g_plugins[i].module_handle) {
            FreeLibrary(g_plugins[i].module_handle);
            g_plugins[i].module_handle = NULL;
        }
    }
    g_current_plugin_id = 0;
    g_current_plugin_entry = NULL;
    ReleaseSRWLockExclusive(&g_plugin_lock);
    return overall_hr;
}

int TE_PluginLoaderGetCount(void) {
    return g_plugin_count;
}

TE_PluginEntry* TE_PluginLoaderGetEntry(int index) {
    if (index >= 0 && index < g_plugin_count) {
        return &g_plugins[index];
    }
    return NULL;
}

TE_PluginEntry* TE_PluginLoaderFindByName(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < g_plugin_count; i++) {
        if (g_plugins[i].metadata && g_plugins[i].metadata->name && strcmp(g_plugins[i].metadata->name, name) == 0) {
            return &g_plugins[i];
        }
    }
    return NULL;
}

HRESULT TE_PluginLoaderEnablePluginByName(const char* name) {
    AcquireSRWLockExclusive(&g_plugin_lock);
    TE_PluginEntry* entry = TE_PluginLoaderFindByName(name);
    if (!entry) {
        ReleaseSRWLockExclusive(&g_plugin_lock);
        return TE_E_INVALIDARG;
    }
    if (!entry->initialized) {
        ReleaseSRWLockExclusive(&g_plugin_lock);
        return TE_E_FAIL;
    }
    if (entry->enabled) {
        ReleaseSRWLockExclusive(&g_plugin_lock);
        return TE_S_OK;
    }
    
    g_current_plugin_id = entry->plugin_id;
    g_current_plugin_entry = entry;
    HRESULT res = TE_FaultIsolatedCall(&entry->fault_count, (char*)entry->metadata->name, "Enable", PluginEnableFunc);
    if (SUCCEEDED(res)) {
        entry->enabled = TRUE;
    }
    ReleaseSRWLockExclusive(&g_plugin_lock);
    return res;
}

void TE_PluginLoaderDisablePluginByName(const char* name) {
    AcquireSRWLockExclusive(&g_plugin_lock);
    TE_PluginEntry* entry = TE_PluginLoaderFindByName(name);
    if (!entry || !entry->enabled) {
        ReleaseSRWLockExclusive(&g_plugin_lock);
        return;
    }
    
    g_current_plugin_id = entry->plugin_id;
    g_current_plugin_entry = entry;
    TE_FaultIsolatedCall(&entry->fault_count, (char*)entry->metadata->name, "Disable", PluginDisableFunc);
    entry->enabled = FALSE;
    ReleaseSRWLockExclusive(&g_plugin_lock);
}

void TE_PluginLoaderLockShared(void) {
    AcquireSRWLockShared(&g_plugin_lock);
}

void TE_PluginLoaderUnlockShared(void) {
    ReleaseSRWLockShared(&g_plugin_lock);
}

void TE_PluginLoaderLockExclusive(void) {
    AcquireSRWLockExclusive(&g_plugin_lock);
}

void TE_PluginLoaderUnlockExclusive(void) {
    ReleaseSRWLockExclusive(&g_plugin_lock);
}

