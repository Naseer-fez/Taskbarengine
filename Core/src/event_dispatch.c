#include "core/event_dispatch.h"
#include "core/plugin_loader.h"
#include "core/fault_isolation.h"
#include <windows.h>
#include <sdk/te_log.h>

void TE_EventDispatchInit(TE_EventEntry* table, uint32_t* count)
{
    if (table && count) {
        *count = 0;
        ZeroMemory(table, sizeof(TE_EventEntry) * TE_MAX_SUBSCRIPTIONS);
    }
}

HRESULT TE_EventSubscribeEx(TE_EventEntry* table, uint32_t* count, TE_EventType type, TE_EventCallback cb, void* user_data, uint32_t plugin_id, void* plugin_entry)
{
    if (!table || !count || !cb) return E_POINTER;
    if (*count >= TE_MAX_SUBSCRIPTIONS) {
        TE_LogWrite(TE_LOG_WARN, "Event subscribe failed: max subscriptions (%d) reached", TE_MAX_SUBSCRIPTIONS);
        return E_OUTOFMEMORY;
    }

    /* Check if already subscribed */
    for (uint32_t i = 0; i < *count; i++) {
        if (table[i].type == type && table[i].callback == cb && table[i].user_data == user_data) {
            table[i].plugin_entry = plugin_entry;
            return S_OK; /* Already subscribed */
        }
    }

    table[*count].type = type;
    table[*count].callback = cb;
    table[*count].user_data = user_data;
    table[*count].plugin_id = plugin_id;
    table[*count].plugin_entry = plugin_entry;
    (*count)++;

    return S_OK;
}

HRESULT TE_EventSubscribe(TE_EventEntry* table, uint32_t* count, TE_EventType type, TE_EventCallback cb, void* user_data, uint32_t plugin_id)
{
    return TE_EventSubscribeEx(table, count, type, cb, user_data, plugin_id, NULL);
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

void TE_EventDispatchTargeted(const TE_EventEntry* table, uint32_t count, TE_EventType type, const void* event_data, uint32_t target_plugin_id)
{
    if (!table || count == 0) return;

    for (int32_t i = (int32_t)count - 1; i >= 0; i--) {
        if (table[i].type == type && table[i].callback != NULL) {
            if (target_plugin_id != 0 && table[i].plugin_id != 0 && table[i].plugin_id != target_plugin_id) {
                continue; /* Skip event for non-targeted plugins */
            }

            TE_PluginEntry* entry = (TE_PluginEntry*)table[i].plugin_entry;
            if (entry && (!entry->enabled || entry->disabled_by_fault)) {
                continue; /* Skip disabled or fault-disabled plugins */
            }

            TE_FaultIsolationCallEventCallback(entry, table[i].callback, type, event_data, table[i].user_data);
        }
    }
}

void TE_EventDispatch(const TE_EventEntry* table, uint32_t count, TE_EventType type, const void* event_data)
{
    TE_EventDispatchTargeted(table, count, type, event_data, 0);
}
