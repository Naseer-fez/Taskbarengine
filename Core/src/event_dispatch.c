#include "core/event_dispatch.h"
#include <windows.h>
#include <string.h>
#include <sdk/te_log.h>

typedef struct TE_Subscription {
    uint32_t event_type;
    TE_EventCallback callback;
    void* user_data;
    uint32_t plugin_id;
    BOOL active;
} TE_Subscription;

static TE_Subscription g_subscriptions[TE_MAX_SUBSCRIPTIONS];
static uint32_t g_subscription_count = 0;

HRESULT TE_EventDispatchInit(void)
{
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_subscription_count = 0;
    return TE_S_OK;
}

void TE_EventDispatchShutdown(void)
{
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_subscription_count = 0;
}

HRESULT TE_EventDispatchSubscribe(uint32_t event_type, TE_EventCallback callback,
                                   void* user_data, uint32_t plugin_id)
{
    if (!callback) return TE_E_INVALIDARG;
    if (g_subscription_count >= TE_MAX_SUBSCRIPTIONS) return TE_E_FAIL;

    for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        if (!g_subscriptions[i].active) {
            g_subscriptions[i].event_type = event_type;
            g_subscriptions[i].callback = callback;
            g_subscriptions[i].user_data = user_data;
            g_subscriptions[i].plugin_id = plugin_id;
            g_subscriptions[i].active = TRUE;
            g_subscription_count++;
            return TE_S_OK;
        }
    }
    return TE_E_FAIL;
}

HRESULT TE_EventDispatchUnsubscribe(uint32_t event_type, TE_EventCallback callback)
{
    if (!callback) return TE_E_INVALIDARG;

    for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        if (g_subscriptions[i].active && 
            g_subscriptions[i].event_type == event_type && 
            g_subscriptions[i].callback == callback) {
            
            g_subscriptions[i].active = FALSE;
            g_subscription_count--;
            
            // Compact array
            for (uint32_t j = i; j < TE_MAX_SUBSCRIPTIONS - 1; ++j) {
                g_subscriptions[j] = g_subscriptions[j + 1];
            }
            g_subscriptions[TE_MAX_SUBSCRIPTIONS - 1].active = FALSE;
            
            return TE_S_OK;
        }
    }
    return TE_E_FAIL;
}

void TE_EventDispatchFire(uint32_t event_type, const void* event_data)
{
    for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        if (g_subscriptions[i].active && g_subscriptions[i].event_type == event_type) {
            __try {
                g_subscriptions[i].callback(event_type, event_data, g_subscriptions[i].user_data);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                TE_LogWrite(TE_LOG_ERROR, "EventDispatch", "Exception caught in event callback");
            }
        }
    }
}

void TE_EventDispatchRemoveByPlugin(uint32_t plugin_id)
{
    uint32_t write_idx = 0;
    uint32_t new_count = 0;
    
    for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        if (g_subscriptions[i].active) {
            if (g_subscriptions[i].plugin_id == plugin_id) {
                g_subscriptions[i].active = FALSE;
            } else {
                if (write_idx != i) {
                    g_subscriptions[write_idx] = g_subscriptions[i];
                    g_subscriptions[i].active = FALSE;
                }
                write_idx++;
                new_count++;
            }
        }
    }
    
    // Clear the rest
    for (uint32_t i = write_idx; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        g_subscriptions[i].active = FALSE;
    }
    
    g_subscription_count = new_count;
}

uint32_t TE_EventDispatchGetCount(void)
{
    return g_subscription_count;
}
