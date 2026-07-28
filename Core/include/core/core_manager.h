#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque structure representing internal Core Manager state.
 */
typedef struct TE_CoreState TE_CoreState;

/**
 * @brief Initialize the Core Manager, load config, start watcher, and discover plugins.
 * @param hinstance Engine DLL module instance handle.
 * @return S_OK on success, or failure HRESULT.
 */
HRESULT TE_CoreManagerInit(HINSTANCE hinstance);

/**
 * @brief Shutdown the Core Manager, stop watcher, disable plugins, and free state.
 */
void TE_CoreManagerShutdown(void);

/**
 * @brief Disable plugins and release core state without stopping the IPC server thread.
 * @note Used by the IPC server so it can send SHUTDOWN_COMPLETE before exiting.
 */
void TE_CoreManagerShutdownFromIpc(void);

/**
 * @brief Re-read config and dispatch hot-reload events for changed plugin sections.
 */
void TE_CoreManagerReloadConfig(void);

/**
 * @brief Enable or disable a plugin by metadata name.
 */
HRESULT TE_CoreManagerSetPluginEnabledByName(const char* plugin_name, bool enabled);

/**
 * @brief Write a newline-separated plugin status list into a caller-provided buffer.
 */
uint32_t TE_CoreManagerBuildPluginList(char* buffer, size_t buffer_len);

/**
 * @brief Process config file change notification, re-parse config, and dispatch targeted events.
 * @param core_state_ptr Opaque pointer to TE_CoreState instance.
 */
void TE_CoreManagerOnConfigChanged(void* core_state_ptr);

/**
 * @brief Get the active plugin ID context.
 * @return Active 1-based plugin ID, or 0 if none active.
 */
uint32_t TE_CoreManagerGetCurrentPluginId(void);

/**
 * @brief Set the active plugin ID context for event subscription attribution.
 * @param plugin_id Active 1-based plugin ID, or 0 to clear.
 */
void TE_CoreManagerSetCurrentPluginId(uint32_t plugin_id);

#ifdef __cplusplus
}
#endif
