#include "core/event_dispatch.h"
#include <windows.h>
#include <sdk/te_log.h>

void TE_EventDispatchInit(TE_EventEntry* table, uint32_t* count)
{
    if (table && count) {
        *count = 0;
        ZeroMemory(table, sizeof(TE_EventEntry) * TE_MAX_SUBSCRIPTIONS);
    }
}

HRESULT TE_EventSubscribe(TE_EventEntry* table, uint32_t* count, TE_EventType type, TE_EventCallback cb, void* user_data, uint32_t plugin_id)
{
    if (!table || !count || !cb) return E_POINTER;
    if (*count >= TE_MAX_SUBSCRIPTIONS) {
        TE_LogWrite(TE_LOG_WARN, "Event subscribe failed: max subscriptions (%d) reached", TE_MAX_SUBSCRIPTIONS);
        return E_OUTOFMEMORY;
    }

    /* Check if already subscribed */
    for (uint32_t i = 0; i < *count; i++) {
        if (table[i].type == type && table[i].callback == cb && table[i].user_data == user_data) {
            return S_OK; /* Already subscribed */
        }
    }

    table[*count].type = type;
    table[*count].callback = cb;
    table[*count].user_data = user_data;
    table[*count].plugin_id = plugin_id;
    (*count)++;

    return S_OK;
}

HRESULT TE_EventUnsubscribe(TE_EventEntry* table, uint32_t* count, TE_EventType type, TE_EventCallback cb)
{
    if (!table || !count || !cb) return E_POINTER;

    for (uint32_t i = 0; i < *count; i++) {
        if (table[i].type == type && table[i].callback == cb) {
            /* Shift remaining entries left */
            for (uint32_t j = i; j < *count - 1; j++) {
                table[j] = table[j + 1];
            }
            (*count)--;
            ZeroMemory(&table[*count], sizeof(TE_EventEntry));
            return S_OK;
        }
    }

    return S_FALSE; /* Not found */
}

void TE_EventDispatch(const TE_EventEntry* table, uint32_t count, TE_EventType type, const void* event_data)
{
    if (!table || count == 0) return;

    for (uint32_t i = 0; i < count; i++) {
        if (table[i].type == type && table[i].callback != NULL) {
            __try {
                table[i].callback(type, event_data, table[i].user_data);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                TE_LogWrite(TE_LOG_ERROR, "SEH Exception caught in event callback for event type %d", (int)type);
            }
        }
    }
}
