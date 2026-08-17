#pragma once

#include <sdk/te_types.h>
#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_MAX_MSG_SUBSCRIPTIONS 128

/**
 * @brief Initialize the Win32 message filter subscription registry.
 *
 * Resets all subscription entries and initializes synchronization primitives.
 *
 * @return S_OK on success.
 * @note Thread safety: Thread-safe.
 */
HRESULT TE_MsgFilterInit(void);

/**
 * @brief Subscribe a plugin to a specific Win32 window message on Shell_TrayWnd.
 *
 * If the subscription already exists for the given plugin and message ID,
 * this function returns S_OK (idempotent).
 *
 * @param plugin_id 1-based plugin ID.
 * @param win_msg Win32 message identifier (e.g. WM_MOUSEMOVE).
 * @return S_OK on success, E_OUTOFMEMORY if the subscription table is full,
 *         or E_INVALIDARG if plugin_id is 0.
 * @note Thread safety: Thread-safe.
 */
HRESULT TE_MsgFilterSubscribe(uint32_t plugin_id, UINT win_msg);

/**
 * @brief Unsubscribe a plugin from a specific Win32 window message.
 *
 * @param plugin_id 1-based plugin ID.
 * @param win_msg Win32 message identifier.
 * @return S_OK if removed, S_FALSE if no matching subscription found.
 * @note Thread safety: Thread-safe.
 */
HRESULT TE_MsgFilterUnsubscribe(uint32_t plugin_id, UINT win_msg);

/**
 * @brief Remove all message subscriptions owned by a specific plugin.
 *
 * Called automatically during plugin disable or unload.
 *
 * @param plugin_id 1-based plugin ID.
 * @note Thread safety: Thread-safe.
 */
void TE_MsgFilterUnsubscribeAll(uint32_t plugin_id);

/**
 * @brief Check if any active plugin is subscribed to the specified Win32 message.
 *
 * Designed for fast O(1) or minimal-scan lookup on the subclass window proc hot path.
 *
 * @param win_msg Win32 message identifier to test.
 * @return TRUE if at least one plugin subscribes to this message, FALSE otherwise.
 * @note Thread safety: Thread-safe (shared read lock).
 */
BOOL TE_MsgFilterHasSubscriber(UINT win_msg);

/**
 * @brief Retrieve the current count of active message subscriptions.
 *
 * Useful for diagnostics and unit testing.
 *
 * @return Number of active subscriptions in the table.
 */
uint32_t TE_MsgFilterGetCount(void);

#ifdef __cplusplus
}
#endif
