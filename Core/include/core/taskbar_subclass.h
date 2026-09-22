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

#define TE_MAX_TRACKED_TASKBARS 16

typedef struct TE_TrackedTaskbar {
    HWND hwnd;
    HMONITOR monitor;
    BOOL is_primary;
    BOOL was_in_taskbar;
    BOOL was_dragging;
    POINT last_pt;
} TE_TrackedTaskbar;

/**
 * Dynamically discover and subclass all primary (Shell_TrayWnd) and
 * secondary (Shell_SecondaryTrayWnd) taskbars in the current Explorer process.
 * Re-validates existing tracked taskbars and cleans up destroyed handles.
 *
 * @return Total number of currently tracked taskbars.
 */
uint32_t TE_TaskbarTrackAll(void);

/**
 * Get the count of currently tracked taskbars.
 */
uint32_t TE_TaskbarGetTrackedCount(void);

/**
 * Retrieve information about a tracked taskbar by index.
 * @param index 0-based index (< TE_TaskbarGetTrackedCount()).
 * @param out_taskbar Output pointer to receive taskbar info.
 * @return TRUE if found and populated, FALSE otherwise.
 */
BOOL TE_TaskbarGetTracked(uint32_t index, TE_TrackedTaskbar* out_taskbar);

/**
 * Check whether a window is currently in the tracked taskbar list.
 */
BOOL TE_TaskbarIsTracked(HWND hwnd);

/**
 * Given a window or child control, find the root tracked taskbar HWND.
 * @param hwnd Window handle to test.
 * @return Root tracked taskbar HWND, or NULL if not belonging to a taskbar.
 */
HWND TE_TaskbarFindRoot(HWND hwnd);

/**
 * Remove subclass and untrack all secondary (and optionally primary) taskbars.
 */
void TE_TaskbarUntrackAll(void);

/**
 * Remove subclass and untrack all taskbars, killing secondary timers,
 * optionally skipping immediate subclass removal for except_hwnd so that
 * Comctl32 can remove it during WM_NCDESTROY without corrupting the subclass chain.
 */
void TE_TaskbarUntrackAllExcept(HWND except_hwnd);

#ifdef __cplusplus
}
#endif
