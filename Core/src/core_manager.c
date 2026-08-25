#include "core/core_manager.h"
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <string.h>
#include "core/config.h"
#include "core/event_dispatch.h"
#include "core/plugin_loader.h"
#include "core/config_watcher.h"
#include "core/state_store.h"
#include <sdk/te_log.h>
#include <sdk/te_log_impl.h>
#include <sdk/te_jsonc.h>
#include <sdk/te_events.h>
#include <sdk/te_plugin.h>
#include "core/engine.h"

static struct {
    BOOL initialized;
    HWND taskbar_hwnd;
    cJSON* config_root;
    wchar_t config_path[MAX_PATH];
    wchar_t log_dir[MAX_PATH];
    wchar_t modules_dir[MAX_PATH];
    wchar_t config_dir[MAX_PATH];
    uint32_t dpi;
} g_core;

typedef UINT (WINAPI *GetDpiForWindow_t)(HWND);
/* TE_JsoncFree is declared in te_jsonc.h */

HRESULT TE_CoreManagerInit(HWND taskbar_hwnd) {
    if (g_core.initialized) return TE_S_OK;
    memset(&g_core, 0, sizeof(g_core));
    g_core.taskbar_hwnd = taskbar_hwnd;
    
    GetDpiForWindow_t pGetDpiForWindow = (GetDpiForWindow_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (pGetDpiForWindow) {
        g_core.dpi = pGetDpiForWindow(taskbar_hwnd);
    } else {
        g_core.dpi = 96;
    }
    
    wchar_t dll_path[MAX_PATH];
    if (GetModuleFileNameW(TE_EngineGetInstance(), dll_path, MAX_PATH)) {
        PathRemoveFileSpecW(dll_path);
        
        swprintf(g_core.config_path, MAX_PATH, L"%s\\..\\Config\\default_config.jsonc", dll_path);
        swprintf(g_core.config_dir, MAX_PATH, L"%s\\..\\Config", dll_path);
        swprintf(g_core.log_dir, MAX_PATH, L"%s\\..\\Logs", dll_path);
        swprintf(g_core.modules_dir, MAX_PATH, L"%s\\..\\Modules", dll_path);
    }
    
    CreateDirectoryW(g_core.log_dir, NULL);
    TE_LogInit(g_core.log_dir, TE_LOG_INFO);
    TE_LogWrite(TE_LOG_INFO, "CoreManager", "Core Manager initializing");
    
    TE_EventDispatchInit();
    
    cJSON* root = NULL;
    TE_ConfigLoad(g_core.config_path, &root);
    g_core.config_root = root;
    
    TE_PluginLoaderInit();
    TE_PluginLoaderScanAndLoad(g_core.modules_dir);
    TE_PluginLoaderInitializeAll(g_core.taskbar_hwnd, g_core.dpi, g_core.config_root);
    TE_PluginLoaderEnableAll();
    
    TE_ConfigWatcherStart(g_core.config_dir, g_core.taskbar_hwnd);
    
    g_core.initialized = TRUE;
    TE_LogWrite(TE_LOG_INFO, "CoreManager", "Core Manager initialized successfully");
    
    return TE_S_OK;
}

void TE_CoreManagerShutdown(void) {
    if (!g_core.initialized) return;
    
    TE_ConfigWatcherStop();
    TE_PluginLoaderDisableAll();
    TE_PluginLoaderShutdownAll();
    TE_PluginLoaderShutdown();
    TE_EventDispatchShutdown();
    
    if (g_core.config_root) {
        TE_JsoncFree(g_core.config_root);
        g_core.config_root = NULL;
    }
    TE_LogWrite(TE_LOG_INFO, "CoreManager", "Core Manager shut down");
    TE_LogFlush();
    TE_LogShutdown();
    memset(&g_core, 0, sizeof(g_core));
}

HRESULT TE_CoreManagerReloadConfig(void) {
    if (!g_core.initialized) return TE_E_FAIL;
    cJSON* new_root = NULL;
    if (FAILED(TE_ConfigLoad(g_core.config_path, &new_root)) || !new_root) {
        TE_LogWrite(TE_LOG_WARNING, "CoreManager", "Failed to parse new config");
        return TE_E_FAIL;
    }
    
    const char* changed_names[TE_MAX_PLUGINS];
    int changed_count = 0;
    TE_ConfigDiffPlugins(g_core.config_root, new_root,
                         changed_names, &changed_count, TE_MAX_PLUGINS);
    
    cJSON* old_root = g_core.config_root;
    g_core.config_root = new_root;
    if (old_root) {
        TE_JsoncFree(old_root);
    }
    
    for (int i = 0; i < changed_count; i++) {
        TE_ConfigChangedData data;
        data.plugin_name = changed_names[i];
        data.new_config = TE_ConfigGetPluginSection(g_core.config_root, changed_names[i]);
        TE_EventDispatchFire(TE_EVENT_CONFIG_CHANGED, &data);
    }
    
    return TE_S_OK;
}

void TE_CoreManagerHandleCommand(int cmd_type, void* payload) {
    (void)payload;
    switch (cmd_type) {
        case TE_CMD_RELOAD_CONFIG:
            TE_CoreManagerReloadConfig();
            break;
        case TE_CMD_SHUTDOWN:
            TE_CoreManagerShutdown();
            break;
    }
}

HWND TE_CoreManagerGetTaskbarHwnd(void) {
    return g_core.taskbar_hwnd;
}

BOOL TE_CoreManagerIsInitialized(void) {
    return g_core.initialized;
}
