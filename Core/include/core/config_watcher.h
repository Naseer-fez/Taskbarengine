#pragma once
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start watching a directory for file changes (config hot-reload).
 * Spawns a background thread using ReadDirectoryChangesW with 100ms debounce.
 * On change, marshals TE_CMD_RELOAD_CONFIG to the UI thread via PostMessage.
 *
 * @param config_dir    Directory path to watch. Must not be NULL.
 * @param taskbar_hwnd  HWND to post WM_TE_IPC_COMMAND to.
 * @return TE_S_OK on success.
 * @note Thread Safety: Call once during startup on UI thread.
 */
HRESULT TE_ConfigWatcherStart(const wchar_t* config_dir, HWND taskbar_hwnd);

/**
 * Stop the config watcher thread and release resources.
 * Blocks until the watcher thread exits.
 *
 * @note Thread Safety: Call once during shutdown on UI thread.
 */
void TE_ConfigWatcherStop(void);

#ifdef __cplusplus
}
#endif
