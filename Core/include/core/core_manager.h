#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque structure representing internal Core Manager state.
 *
 * ## Ownership & Threading Model
 * - **Singleton Instance**: A single heap-allocated `TE_CoreState` instance is created
 *   per injected Explorer process during Phase A and freed during Shutdown.
 * - **Thread Affinity**: Core manager initialization and state mutations execute
 *   strictly on Explorer's main UI thread (the message pump owning `Shell_TrayWnd`).
 * - **Lifecycle**:
 *   1. `TE_CoreManagerInitPhaseA`: Allocates state, initializes event tables, installs subclass.
 *   2. `TE_CoreManagerInitPhaseB`: Triggered via `WM_TE_INIT` outside loader lock; loads config,
 *      message filters, timers, scans plugins, and enables active plugins.
 *   3. `TE_CoreManagerShutdown`: Tears down timers, watchers, plugins, subclass, and frees state.
 */
typedef struct TE_CoreState TE_CoreState;

/**
 * @brief Initialize the Core Manager Phase A: allocate state, subclass taskbar.
 * @param hinstance Engine DLL module instance handle.
 * @return S_OK on success, or failure HRESULT.
 * @note Must be called on the UI thread owning Shell_TrayWnd.
 */
HRESULT TE_CoreManagerInitPhaseA(HINSTANCE hinstance);

/**
 * @brief Initialize the Core Manager Phase B: config, watcher, plugins, IPC.
 * @return S_OK on success, or failure HRESULT.
 * @note Executed outside loader lock in response to deferred WM_TE_INIT message.
 */
HRESULT TE_CoreManagerInitPhaseB(void);

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

/**
 * @brief Serialize all loaded plugin settings schemas into a JSON string.
 * @param buffer Output buffer for the JSON string.
 * @param buffer_len Size of the output buffer.
 * @return Number of bytes written (including null terminator), or 0 on failure.
 */
uint32_t TE_CoreManagerBuildSettingsSchema(char* buffer, size_t buffer_len);

/**
 * @brief Serialize current performance statistics into a JSON string.
 * @param buffer Output buffer for the JSON string.
 * @param buffer_len Size of the output buffer.
 * @return Number of bytes written (including null terminator), or 0 on failure.
 */
uint32_t TE_CoreManagerBuildPerfStats(char* buffer, size_t buffer_len);

/**
 * @brief Evaluates whether a plugin's configuration section indicates it is enabled.
 */
struct cJSON;
bool TE_CoreManagerIsPluginEnabledInConfig(const struct cJSON* config);

#ifdef __cplusplus
}
#endif
