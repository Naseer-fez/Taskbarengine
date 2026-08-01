#pragma once

#include <sdk/te_types.h>
#include "core/event_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WM_TE_IPC_COMMAND (WM_APP + 102)

#define TE_IPC_CMD_RELOAD_CONFIG 1
#define TE_IPC_CMD_ENABLE_PLUGIN 2
#define TE_IPC_CMD_DISABLE_PLUGIN 3

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
