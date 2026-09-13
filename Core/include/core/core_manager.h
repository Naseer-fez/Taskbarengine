#pragma once
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** IPC command types marshaled via WM_TE_IPC_COMMAND wParam. */
#define TE_CMD_RELOAD_CONFIG  1
#define TE_CMD_ENABLE_PLUGIN  2
#define TE_CMD_DISABLE_PLUGIN 3
#define TE_CMD_SHUTDOWN       4

/**
 * Initialize the Core Manager (Phase B initialization).
 * Loads config, starts logging, initializes event dispatch, starts config
 * watcher, scans and loads plugins, enables all plugins.
 *
 * @param taskbar_hwnd  Handle to Shell_TrayWnd.
 * @return TE_S_OK on success, or error HRESULT.
 * @note Thread Safety: Must be called on UI thread.
 */
HRESULT TE_CoreManagerInit(HWND taskbar_hwnd);

/**
 * Shut down the Core Manager.
 * Disables all plugins (reverse priority), shuts down plugins, stops
 * config watcher, stops logger, frees config.
 *
 * @note Thread Safety: Must be called on UI thread.
 */
void TE_CoreManagerShutdown(void);

/**
 * Reload configuration from disk and dispatch CONFIG_CHANGED events.
 * Called on the UI thread (marshaled from config watcher or IPC).
 *
 * @return TE_S_OK on success, or error HRESULT.
 * @note Thread Safety: Must be called on UI thread.
 */
HRESULT TE_CoreManagerReloadConfig(void);

/**
 * Handle a marshaled IPC command on the UI thread.
 * @param cmd_type  Command type (TE_CMD_* constant).
 * @param payload   Command-specific payload (may be NULL).
 * @note Thread Safety: Must be called on UI thread.
 */
void TE_CoreManagerHandleCommand(int cmd_type, void* payload);

/**
 * Get the active taskbar HWND.
 * @return The Shell_TrayWnd handle, or NULL if not initialized.
 */
HWND TE_CoreManagerGetTaskbarHwnd(void);

/**
 * Check whether the core manager has been initialized.
 * @return TRUE if initialized, FALSE otherwise.
 */
BOOL TE_CoreManagerIsInitialized(void);

/**
 * Get the current cached taskbar DPI scaling value.
 * @return Current DPI (e.g., 96, 120, 144, 192).
 */
uint32_t TE_CoreManagerGetDpi(void);

/**
 * Update the cached taskbar DPI scaling value.
 * @param dpi New DPI value.
 */
void TE_CoreManagerSetDpi(uint32_t dpi);

#ifdef __cplusplus
}
#endif
