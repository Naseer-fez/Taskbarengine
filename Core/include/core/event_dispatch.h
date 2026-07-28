#pragma once

#include <sdk/te_types.h>
#include <sdk/te_events.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_MAX_SUBSCRIPTIONS 64

typedef struct TE_EventEntry {
    TE_EventType     type;
    TE_EventCallback callback;
    void*            user_data;
    uint32_t         plugin_id;
    void*            plugin_entry; /* TE_PluginEntry pointer if registered by a plugin */
} TE_EventEntry;

/**
 * @brief Initialize event dispatch subscription table.
 * @param table Pointer to subscription table array.
 * @param count Pointer to subscription count.
 */
void TE_EventDispatchInit(TE_EventEntry* table, uint32_t* count);

/**
 * @brief Subscribe to an engine event type with extended metadata.
 * @param table Pointer to subscription table array.
 * @param count Pointer to subscription count.
 * @param type Event type enum.
 * @param cb Callback function.
 * @param user_data Opaque user context.
 * @param plugin_id ID of subscribing plugin (0 for core/global).
 * @param plugin_entry TE_PluginEntry pointer for fault handling.
 * @return S_OK on success, E_OUTOFMEMORY if table full.
 */
HRESULT TE_EventSubscribeEx(TE_EventEntry* table, uint32_t* count, TE_EventType type, TE_EventCallback cb, void* user_data, uint32_t plugin_id, void* plugin_entry);

/**
 * @brief Subscribe to an engine event type.
 * @param table Pointer to subscription table array.
 * @param count Pointer to subscription count.
 * @param type Event type enum.
 * @param cb Callback function.
 * @param user_data Opaque user context.
 * @param plugin_id ID of subscribing plugin (0 for core/global).
 * @return S_OK on success, E_OUTOFMEMORY if table full.
 */
HRESULT TE_EventSubscribe(TE_EventEntry* table, uint32_t* count, TE_EventType type, TE_EventCallback cb, void* user_data, uint32_t plugin_id);

/**
 * @brief Unsubscribe from an engine event type.
 * @param table Pointer to subscription table array.
 * @param count Pointer to subscription count.
 * @param type Event type enum.
 * @param cb Callback function to remove.
 * @return S_OK on success, S_FALSE if not found.
 */
HRESULT TE_EventUnsubscribe(TE_EventEntry* table, uint32_t* count, TE_EventType type, TE_EventCallback cb);

/**
 * @brief Synchronously dispatch event to all subscribers of event type.
 * @param table Subscription table array.
 * @param count Active subscription count.
 * @param type Event type enum.
 * @param event_data Event payload pointer.
 */
void TE_EventDispatch(const TE_EventEntry* table, uint32_t count, TE_EventType type, const void* event_data);

/**
 * @brief Synchronously dispatch event to subscribers targeting a specific plugin ID.
 * @param table Subscription table array.
 * @param count Active subscription count.
 * @param type Event type enum.
 * @param event_data Event payload pointer.
 * @param target_plugin_id Target plugin ID (0 for all).
 */
void TE_EventDispatchTargeted(const TE_EventEntry* table, uint32_t count, TE_EventType type, const void* event_data, uint32_t target_plugin_id);

#ifdef __cplusplus
}
#endif
