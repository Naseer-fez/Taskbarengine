#pragma once

#include <sdk/te_types.h>
#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_MAX_PLUGINS 32

/**
 * @brief Windows version compatibility status for a loaded plugin.
 */
typedef enum TE_PluginCompatStatus {
    TE_COMPAT_OK,                 /**< Build is supported and within tested range */
    TE_COMPAT_UNTESTED_BUILD,      /**< Build is newer than plugin's max_tested_build */
    TE_COMPAT_UNSUPPORTED_BUILD    /**< Build is older than plugin's min_build */
} TE_PluginCompatStatus;

typedef struct TE_PluginEntry {
    HMODULE                  dll_handle;
    const PluginInterface*   iface;
    const PluginMetadata*    metadata;
    PluginContext*           context;          /* heap-allocated, per-plugin */
    bool                     enabled;
    uint32_t                 fault_count;      /* consecutive timeout counter */
    bool                     disabled_by_fault;
    TE_PluginCompatStatus    compat_status;    /* Windows build compatibility status */
} TE_PluginEntry;

/**
 * @brief Scan modules directory for plugin DLLs, load them, and sort by priority.
 * @param modules_dir Path to Modules/ directory.
 * @param registry Array to store plugin entries.
 * @param count Pointer to receive loaded plugin count.
 * @return S_OK on success, or error HRESULT.
 */
HRESULT TE_PluginLoaderScan(const wchar_t* modules_dir, TE_PluginEntry* registry, uint32_t* count);

/**
 * @brief Enable a loaded plugin by invoking its Enable() callback.
 * @param entry Pointer to plugin registry entry.
 * @return S_OK on success, or error HRESULT.
 */
HRESULT TE_PluginLoaderEnable(TE_PluginEntry* entry);

/**
 * @brief Disable an enabled plugin by invoking its Disable() callback.
 * @param entry Pointer to plugin registry entry.
 * @return S_OK on success, or error HRESULT.
 */
HRESULT TE_PluginLoaderDisable(TE_PluginEntry* entry);

/**
 * @brief Disable and shutdown a plugin, free context, and unload DLL.
 * @param entry Pointer to plugin registry entry.
 * @return S_OK on success, or error HRESULT.
 */
HRESULT TE_PluginLoaderShutdown(TE_PluginEntry* entry);

/**
 * @brief Unload and shutdown all plugins in reverse priority order.
 * @param registry Array of plugin entries.
 * @param count Number of loaded plugins.
 */
void TE_PluginLoaderUnloadAll(TE_PluginEntry* registry, uint32_t count);

#ifdef __cplusplus
}
#endif
