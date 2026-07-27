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
} TE_EventEntry;

void TE_EventDispatchInit(TE_EventEntry* table, uint32_t* count);

HRESULT TE_EventSubscribe(TE_EventEntry* table, uint32_t* count, TE_EventType type, TE_EventCallback cb, void* user_data, uint32_t plugin_id);

HRESULT TE_EventUnsubscribe(TE_EventEntry* table, uint32_t* count, TE_EventType type, TE_EventCallback cb);

void TE_EventDispatch(const TE_EventEntry* table, uint32_t count, TE_EventType type, const void* event_data);

#ifdef __cplusplus
}
#endif
