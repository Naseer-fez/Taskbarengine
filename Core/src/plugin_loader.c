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

static TE_PluginEntry g_plugins[TE_MAX_PLUGINS];
static int g_plugin_count = 0;
static uint32_t g_current_plugin_id = 0;
static TE_PluginEntry* g_current_plugin_entry = NULL;

static HRESULT TE_PluginSubscribeWrapper(uint32_t event_type, void (*callback)(uint32_t, const void*, void*), void* user_data) {
    return TE_EventDispatchSubscribe(event_type, (TE_EventCallback)callback, user_data, g_current_plugin_id);
}

static HRESULT TE_PluginUnsubscribeWrapper(uint32_t event_type, void (*callback)(uint32_t, const void*, void*)) {
    return TE_EventDispatchUnsubscribe(event_type, (TE_EventCallback)callback);
}

static HRESULT TE_PluginSubscribeMessageWrapper(UINT msg) {
    (void)msg;
    /* TODO(Phase3): Wire to TE_TaskbarSubclassSubscribeMessage */
    return TE_S_OK;
}

static HRESULT TE_PluginUnsubscribeMessageWrapper(UINT msg) {
    (void)msg;
    return TE_S_OK;
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
    g_plugin_count = 0;
    g_current_plugin_id = 0;
    g_current_plugin_entry = NULL;
    memset(g_plugins, 0, sizeof(g_plugins));
    return TE_S_OK;
}

void TE_PluginLoaderShutdown(void) {
    TE_PluginLoaderShutdownAll();
    g_plugin_count = 0;
    g_current_plugin_id = 0;
    g_current_plugin_entry = NULL;
}

HRESULT TE_PluginLoaderScanAndLoad(const wchar_t* modules_dir) {
    WIN32_FIND_DATAW find_data;
    wchar_t search_path[MAX_PATH];
    swprintf(search_path, MAX_PATH, L"%s\\*", modules_dir);
    
    HANDLE hFind = FindFirstFileW(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return TE_S_OK;
    
    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) continue;
            
            wchar_t dll_search[MAX_PATH];
            swprintf(dll_search, MAX_PATH, L"%s\\%s\\*.dll", modules_dir, find_data.cFileName);
            
            WIN32_FIND_DATAW dll_data;
            HANDLE hDllFind = FindFirstFileW(dll_search, &dll_data);
            if (hDllFind != INVALID_HANDLE_VALUE) {
                do {
                    if (g_plugin_count >= TE_MAX_PLUGINS) break;
                    
                    wchar_t dll_path[MAX_PATH];
                    swprintf(dll_path, MAX_PATH, L"%s\\%s\\%s", modules_dir, find_data.cFileName, dll_data.cFileName);
                    
                    HMODULE hMod = LoadLibraryW(dll_path);
                    if (hMod) {
                        BOOL loaded = FALSE;
                        GetPluginInterfaceFunc get_interface = (GetPluginInterfaceFunc)(void*)GetProcAddress(hMod, "GetPluginInterface");
                        if (get_interface) {
                            const PluginInterface* iface = get_interface();
                            if (iface) {
                                const PluginMetadata* meta = iface->GetMetadata();
                                if (meta && meta->api_version <= TE_API_VERSION) {
                                    g_plugins[g_plugin_count].module_handle = hMod;
                                    g_plugins[g_plugin_count].interface_ptr = iface;
                                    g_plugins[g_plugin_count].metadata = meta;
                                    g_plugins[g_plugin_count].plugin_id = (uint32_t)g_plugin_count + 1;
                                    wcsncpy_s(g_plugins[g_plugin_count].dll_path, MAX_PATH, dll_path, _TRUNCATE);
                                    g_plugin_count++;
                                    loaded = TRUE;
                                }
                            }
                        }
                        if (!loaded) {
                            FreeLibrary(hMod);
                        }
                    }
                } while (FindNextFileW(hDllFind, &dll_data));
                FindClose(hDllFind);
            }
        }
    } while (FindNextFileW(hFind, &find_data));
    FindClose(hFind);
    
    for (int i = 1; i < g_plugin_count; i++) {
        TE_PluginEntry temp = g_plugins[i];
        int j = i - 1;
        while (j >= 0 && g_plugins[j].metadata->priority > temp.metadata->priority) {
            g_plugins[j + 1] = g_plugins[j];
            j--;
        }
        g_plugins[j + 1] = temp;
    }
    
    return TE_S_OK;
}

static PluginContext g_ctx;
static HRESULT PluginInitFunc(void) {
    if (g_current_plugin_entry && g_current_plugin_entry->interface_ptr && g_current_plugin_entry->interface_ptr->Initialize) {
        return g_current_plugin_entry->interface_ptr->Initialize(&g_ctx);
    }
    return TE_S_OK;
}

HRESULT TE_PluginLoaderInitializeAll(HWND taskbar_hwnd, uint32_t dpi, const struct cJSON* config_root) {
    for (int i = 0; i < g_plugin_count; i++) {
        g_ctx.struct_size = sizeof(PluginContext);
        g_ctx.api_version = TE_API_VERSION;
        g_ctx.taskbar_hwnd = taskbar_hwnd;
        g_ctx.monitor = MonitorFromWindow(taskbar_hwnd, MONITOR_DEFAULTTOPRIMARY);
        g_ctx.dpi = dpi;
        g_ctx.config = TE_ConfigGetPluginSection((cJSON*)config_root, (char*)g_plugins[i].metadata->name);
        g_ctx.log = TE_LogWrite;
        g_ctx.subscribe = TE_PluginSubscribeWrapper;
        g_ctx.unsubscribe = TE_PluginUnsubscribeWrapper;
        g_ctx.request_redraw = TE_PluginRequestRedrawWrapper;
        g_ctx.publish_state = (PublishStateFunc)TE_StatePublish;
        g_ctx.query_state = (QueryStateFunc)TE_StateQuery;
        g_ctx.core_opaque = &g_plugins[i];
        g_ctx.subscribe_message = TE_PluginSubscribeMessageWrapper;
        g_ctx.unsubscribe_message = TE_PluginUnsubscribeMessageWrapper;
        g_ctx.register_timer = TE_PluginRegisterTimerWrapper;
        g_ctx.cancel_timer = TE_PluginCancelTimerWrapper;
        
        g_current_plugin_id = g_plugins[i].plugin_id;
        g_current_plugin_entry = &g_plugins[i];
        
        HRESULT res = TE_FaultIsolatedCall(&g_plugins[i].fault_count, (char*)g_plugins[i].metadata->name, "Initialize", PluginInitFunc);
        if (SUCCEEDED(res)) {
            g_plugins[i].initialized = TRUE;
        }
    }
    return TE_S_OK;
}

static HRESULT PluginEnableFunc(void) {
    if (g_current_plugin_entry && g_current_plugin_entry->interface_ptr && g_current_plugin_entry->interface_ptr->Enable) {
        return g_current_plugin_entry->interface_ptr->Enable();
    }
    return TE_S_OK;
}

HRESULT TE_PluginLoaderEnableAll(void) {
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
    return TE_S_OK;
}

static HRESULT PluginDisableFunc(void) {
    if (g_current_plugin_entry && g_current_plugin_entry->interface_ptr && g_current_plugin_entry->interface_ptr->Disable) {
        return g_current_plugin_entry->interface_ptr->Disable();
    }
    return TE_S_OK;
}

void TE_PluginLoaderDisableAll(void) {
    for (int i = g_plugin_count - 1; i >= 0; i--) {
        if (g_plugins[i].enabled) {
            g_current_plugin_id = g_plugins[i].plugin_id;
            g_current_plugin_entry = &g_plugins[i];
            TE_FaultIsolatedCall(&g_plugins[i].fault_count, (char*)g_plugins[i].metadata->name, "Disable", PluginDisableFunc);
            g_plugins[i].enabled = FALSE;
        }
    }
}

void TE_PluginLoaderShutdownAll(void) {
    for (int i = 0; i < g_plugin_count; i++) {
        TE_EventDispatchRemoveByPlugin(g_plugins[i].plugin_id);
        if (g_plugins[i].initialized) {
            if (g_plugins[i].interface_ptr && g_plugins[i].interface_ptr->Shutdown) {
                g_plugins[i].interface_ptr->Shutdown();
            }
            g_plugins[i].initialized = FALSE;
        }
        if (g_plugins[i].module_handle) {
            FreeLibrary(g_plugins[i].module_handle);
            g_plugins[i].module_handle = NULL;
        }
    }
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
    TE_PluginEntry* entry = TE_PluginLoaderFindByName(name);
    if (!entry) return TE_E_INVALIDARG;
    if (!entry->initialized) return TE_E_FAIL;
    if (entry->enabled) return TE_S_OK;
    
    g_current_plugin_id = entry->plugin_id;
    g_current_plugin_entry = entry;
    HRESULT res = TE_FaultIsolatedCall(&entry->fault_count, (char*)entry->metadata->name, "Enable", PluginEnableFunc);
    if (SUCCEEDED(res)) {
        entry->enabled = TRUE;
    }
    return res;
}

void TE_PluginLoaderDisablePluginByName(const char* name) {
    TE_PluginEntry* entry = TE_PluginLoaderFindByName(name);
    if (!entry) return;
    if (!entry->enabled) return;
    
    g_current_plugin_id = entry->plugin_id;
    g_current_plugin_entry = entry;
    TE_FaultIsolatedCall(&entry->fault_count, (char*)entry->metadata->name, "Disable", PluginDisableFunc);
    entry->enabled = FALSE;
}
