#pragma once
#include <sdk/te_types.h>
#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of plugins that can be loaded. */
#define TE_MAX_PLUGINS 32

/**
 * Runtime state for a loaded plugin.
 */
typedef struct TE_PluginEntry {
    HMODULE module_handle;                  /**< DLL module handle */
    const PluginInterface* interface_ptr;    /**< Plugin vtable */
    const PluginMetadata* metadata;          /**< Plugin metadata */
    uint32_t plugin_id;                      /**< Unique plugin ID (1-based index) */
    BOOL enabled;                           /**< Whether plugin is currently enabled */
    int fault_count;                        /**< SEH fault counter for 3-strike policy */
    BOOL initialized;                       /**< Whether Initialize() succeeded */
    wchar_t dll_path[MAX_PATH];             /**< Full path to the plugin DLL */
} TE_PluginEntry;

/** Initialize the plugin loader subsystem. */
HRESULT TE_PluginLoaderInit(void);

/** Shut down the plugin loader, freeing all DLLs. */
void TE_PluginLoaderShutdown(void);

/**
 * Scan a directory for plugin DLLs and load them.
 * Sorts loaded plugins by priority (ascending).
 * @param modules_dir  Path to the Modules directory.
 * @return TE_S_OK on success (even if some plugins fail to load).
 */
HRESULT TE_PluginLoaderScanAndLoad(const wchar_t* modules_dir);

/**
 * Call Initialize() on all loaded plugins with the given context.
 * @param taskbar_hwnd  Shell_TrayWnd handle.
 * @param dpi           Current DPI.
 * @param config_root   Parsed config root.
 * @return TE_S_OK.
 */
HRESULT TE_PluginLoaderInitializeAll(HWND taskbar_hwnd, uint32_t dpi,
                                      const struct cJSON* config_root);

/** Enable all loaded and initialized plugins in priority order (ascending). */
HRESULT TE_PluginLoaderEnableAll(void);

/** Disable all enabled plugins in reverse priority order (descending). */
void TE_PluginLoaderDisableAll(void);

/** Call Shutdown() on all initialized plugins and FreeLibrary all DLLs. */
void TE_PluginLoaderShutdownAll(void);

/** Get the number of loaded plugins. */
int TE_PluginLoaderGetCount(void);

/** Get a plugin entry by index. Returns NULL if out of range. */
TE_PluginEntry* TE_PluginLoaderGetEntry(int index);

/** Find a plugin by name. Returns NULL if not found. */
TE_PluginEntry* TE_PluginLoaderFindByName(const char* name);

#ifdef __cplusplus
}
#endif
