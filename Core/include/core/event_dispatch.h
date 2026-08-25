#pragma once
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of concurrent event subscriptions. */
#define TE_MAX_SUBSCRIPTIONS 64

/** Event callback function signature. */
typedef void (*TE_EventCallback)(uint32_t event_type, const void* event_data, void* user_data);

/** Initialize the event dispatch subsystem. */
HRESULT TE_EventDispatchInit(void);

/** Shut down the event dispatch subsystem. */
void TE_EventDispatchShutdown(void);

/**
 * Subscribe to an event type.
 * @param event_type  Event type from TE_EventType enum.
 * @param callback    Callback function invoked on event.
 * @param user_data   Opaque pointer passed to callback.
 * @param plugin_id   ID of the subscribing plugin (for cleanup).
 * @return TE_S_OK or TE_E_FAIL if table is full.
 * @note Thread Safety: Must be called on UI thread.
 */
HRESULT TE_EventDispatchSubscribe(uint32_t event_type, TE_EventCallback callback,
                                   void* user_data, uint32_t plugin_id);

/**
 * Unsubscribe from an event.
 * @param event_type  Event type.
 * @param callback    The callback to remove.
 * @return TE_S_OK if found and removed, TE_E_FAIL if not found.
 * @note Thread Safety: Must be called on UI thread.
 */
HRESULT TE_EventDispatchUnsubscribe(uint32_t event_type, TE_EventCallback callback);

/**
 * Fire an event to all matching subscribers. Synchronous, SEH-wrapped.
 * @param event_type  Event type.
 * @param event_data  Pointer to event payload struct (type-specific).
 * @note Thread Safety: Must be called on UI thread.
 */
void TE_EventDispatchFire(uint32_t event_type, const void* event_data);

/**
 * Remove all subscriptions for a specific plugin.
 * @param plugin_id   Plugin ID whose subscriptions to remove.
 * @note Thread Safety: Must be called on UI thread.
 */
void TE_EventDispatchRemoveByPlugin(uint32_t plugin_id);

/**
 * Get current number of active subscriptions.
 * @return Number of entries in the subscription table.
 */
uint32_t TE_EventDispatchGetCount(void);

#ifdef __cplusplus
}
#endif
