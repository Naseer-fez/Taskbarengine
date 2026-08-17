#pragma once

#include <sdk/te_types.h>
#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WM_TE_TIMER_FIRE (WM_APP + 104)

/**
 * @brief Initialize the global timer subsystem.
 *
 * Creates the backing Win32 timer queue and stores the notify window handle
 * for UI-thread callback marshaling.
 *
 * @param notify_hwnd Window handle (Shell_TrayWnd) to receive WM_TE_TIMER_FIRE messages.
 *                    If NULL, timer callbacks fire on thread pool directly (test mode).
 * @return S_OK on success, or E_FAIL if timer queue creation failed.
 * @note Thread safety: Thread-safe.
 */
HRESULT TE_TimerInit(HWND notify_hwnd);

/**
 * @brief Register a timer for a plugin.
 *
 * @param callback Callback function to invoke when the timer fires.
 * @param user_data Opaque pointer passed to the callback.
 * @param interval_ms Timer interval in milliseconds.
 * @param recurring TRUE for repeating timer, FALSE for one-shot timer.
 * @param owner_plugin_id 1-based plugin ID owning this timer.
 * @param[out] out_timer_id Optional pointer to receive the assigned timer ID.
 * @return S_OK on success, E_OUTOFMEMORY if timer pool is full, E_INVALIDARG if invalid params.
 * @note Thread safety: Thread-safe.
 */
HRESULT TE_TimerCreate(TE_TimerCallback callback, void* user_data,
                       uint32_t interval_ms, BOOL recurring,
                       uint32_t owner_plugin_id, uint32_t* out_timer_id);

/**
 * @brief Cancel an active timer by its callback and owner plugin ID.
 *
 * @param callback Callback pointer used during registration.
 * @param owner_plugin_id 1-based plugin ID.
 * @return S_OK if at least one matching timer was cancelled, S_FALSE if not found.
 * @note Thread safety: Thread-safe.
 */
HRESULT TE_TimerCancelByCallback(TE_TimerCallback callback, uint32_t owner_plugin_id);

/**
 * @brief Cancel a specific timer by its timer ID.
 *
 * @param timer_id Timer ID returned by TE_TimerCreate.
 * @return S_OK on success, S_FALSE if timer ID was not active.
 * @note Thread safety: Thread-safe.
 */
HRESULT TE_TimerCancelById(uint32_t timer_id);

/**
 * @brief Cancel all active timers owned by a specific plugin.
 *
 * Called automatically during plugin disable or unload.
 *
 * @param plugin_id 1-based plugin ID.
 * @note Thread safety: Thread-safe.
 */
void TE_TimerCancelAllForPlugin(uint32_t plugin_id);

/**
 * @brief Dispatch a WM_TE_TIMER_FIRE message on the UI thread.
 *
 * Invoked by TaskbarSubclassProc on the Explorer message pump. Validates
 * timer state and executes the registered callback safely on the UI thread.
 *
 * @param wParam Timer ID or entry token passed with WM_TE_TIMER_FIRE.
 * @param lParam Reserved (0).
 * @note Thread safety: Must be called on the UI thread.
 */
void TE_TimerDispatchMessage(WPARAM wParam, LPARAM lParam);

/**
 * @brief Shut down the timer subsystem and cancel all active timers.
 *
 * Closes the timer queue and frees all allocated resources.
 *
 * @note Thread safety: Thread-safe.
 */
void TE_TimerShutdown(void);

/**
 * @brief Retrieve the count of active timers.
 *
 * @return Number of currently active timers.
 */
uint32_t TE_TimerGetActiveCount(void);

#ifdef __cplusplus
}
#endif
