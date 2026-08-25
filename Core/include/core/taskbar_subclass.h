#pragma once
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Install the full TaskbarEngine subclass on Shell_TrayWnd.
 * Replaces the minimal Phase 1 subclass with full message handling.
 *
 * @param taskbar_hwnd  Handle to Shell_TrayWnd.
 * @return TE_S_OK on success.
 * @note Thread Safety: Must be called on UI thread.
 */
HRESULT TE_TaskbarSubclassInstall(HWND taskbar_hwnd);

/**
 * Remove the TaskbarEngine subclass from Shell_TrayWnd.
 * @param taskbar_hwnd  Handle to Shell_TrayWnd.
 * @note Thread Safety: Must be called on UI thread.
 */
void TE_TaskbarSubclassRemove(HWND taskbar_hwnd);

/**
 * The subclass window procedure. Handles WM_TE_INIT, WM_TE_IPC_COMMAND,
 * WM_DPICHANGED, WM_DISPLAYCHANGE, WM_DESTROY, WM_ENDSESSION, and
 * message-filter forwarding to subscribed plugins.
 */
LRESULT CALLBACK TE_TaskbarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam, UINT_PTR uIdSubclass,
                                         DWORD_PTR dwRefData);

/**
 * Subscribe a Win32 message to the message filter table.
 * Subscribed messages are forwarded to plugins via the event dispatch system.
 * @param msg  Win32 message identifier.
 * @return TE_S_OK on success, TE_E_FAIL if table is full.
 */
HRESULT TE_TaskbarSubclassSubscribeMessage(UINT msg);

/**
 * Unsubscribe a Win32 message from the message filter table.
 * @param msg  Win32 message identifier.
 * @return TE_S_OK on success.
 */
HRESULT TE_TaskbarSubclassUnsubscribeMessage(UINT msg);

#ifdef __cplusplus
}
#endif
