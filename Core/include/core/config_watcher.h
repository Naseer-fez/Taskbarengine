#pragma once

#include <sdk/te_types.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WM_TE_CONFIG_CHANGED (WM_APP + 101)

/**
 * @brief Start background directory watcher for config file hot-reload with 100ms debounce.
 * @param config_dir Wide char path to directory containing config.jsonc.
 * @param notify_hwnd Window handle to receive WM_TE_CONFIG_CHANGED notifications.
 * @return S_OK on success, or failure HRESULT.
 */
HRESULT TE_ConfigWatcherStart(const wchar_t* config_dir, HWND notify_hwnd);

/**
 * @brief Stop background config directory watcher and cancel pending debounce timers.
 */
void TE_ConfigWatcherStop(void);

#ifdef __cplusplus
}
#endif
