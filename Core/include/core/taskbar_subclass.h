#pragma once

#include <sdk/te_types.h>
#include "core/event_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Window message sent to taskbar subclass for IPC commands.
 */
#define WM_TE_IPC_COMMAND (WM_APP + 102)

/**
 * @brief IPC command to reload engine configuration.
 */
#define TE_IPC_CMD_RELOAD_CONFIG 1

/**
 * @brief IPC command to enable a plugin by name.
 */
#define TE_IPC_CMD_ENABLE_PLUGIN 2

/**
 * @brief IPC command to disable a plugin by name.
 */
#define TE_IPC_CMD_DISABLE_PLUGIN 3

/**
 * @brief IPC command to shutdown core manager and engine.
 */
#define TE_IPC_CMD_SHUTDOWN 4

/**
 * @brief IPC command to query and format active plugin list.
 */
#define TE_IPC_CMD_GET_PLUGIN_LIST 5

/** @brief IPC command to query the generated settings schema. */
#define TE_IPC_CMD_GET_SETTINGS 6

/** @brief IPC command to query the latest performance statistics. */
#define TE_IPC_CMD_GET_PERF_STATS 7

/**
 * @brief Payload structure for synchronous IPC requests marshaled to UI thread.
 */
typedef struct TE_IpcSyncPayload {
    HANDLE completion_event; /**< Optional completion event handle for legacy event sync. */
    void* buffer;            /**< Destination buffer for data returned from UI thread. */
    size_t buffer_len;       /**< Size of destination buffer in bytes. */
    uint32_t result_code;    /**< Result code or returned byte length from UI thread. */
} TE_IpcSyncPayload;

/**
 * @brief Install window subclass on Shell_TrayWnd to intercept WM messages and dispatch events.
 * @param taskbar_hwnd Handle of Shell_TrayWnd taskbar window.
 * @param event_table Event subscription table array.
 * @param sub_count Pointer to event subscription count.
 * @param core_state_ptr Opaque pointer to TE_CoreState instance.
 * @return S_OK on success, or failure HRESULT.
 */
HRESULT TE_TaskbarSubclassInstall(HWND taskbar_hwnd, TE_EventEntry* event_table, uint32_t* sub_count, void* core_state_ptr);

/**
 * @brief Remove window subclass from Shell_TrayWnd window.
 * @param taskbar_hwnd Handle of Shell_TrayWnd taskbar window.
 */
void TE_TaskbarSubclassRemove(HWND taskbar_hwnd);

#ifdef __cplusplus
}
#endif
