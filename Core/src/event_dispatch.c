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
static CRITICAL_SECTION g_event_cs;

HRESULT TE_EventDispatchInit(void)
{
    InitializeCriticalSection(&g_event_cs);
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_subscription_count = 0;
    return TE_S_OK;
}

void TE_EventDispatchShutdown(void)
{
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_subscription_count = 0;
    DeleteCriticalSection(&g_event_cs);
}

HRESULT TE_EventDispatchSubscribe(uint32_t event_type, TE_EventCallback callback,
                                   void* user_data, uint32_t plugin_id)
{
    if (!callback) return TE_E_INVALIDARG;
    
    HRESULT hr = TE_E_FAIL;
    EnterCriticalSection(&g_event_cs);
    
    if (g_subscription_count < TE_MAX_SUBSCRIPTIONS) {
        for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
            if (!g_subscriptions[i].active) {
                g_subscriptions[i].event_type = event_type;
                g_subscriptions[i].callback = callback;
                g_subscriptions[i].user_data = user_data;
                g_subscriptions[i].plugin_id = plugin_id;
                g_subscriptions[i].active = TRUE;
                g_subscription_count++;
                hr = TE_S_OK;
                break;
            }
        }
    }
    
    LeaveCriticalSection(&g_event_cs);
    return hr;
}

HRESULT TE_EventDispatchUnsubscribe(uint32_t event_type, TE_EventCallback callback)
{
    if (!callback) return TE_E_INVALIDARG;

    HRESULT hr = TE_E_FAIL;
    EnterCriticalSection(&g_event_cs);

    for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        if (g_subscriptions[i].active && 
            g_subscriptions[i].event_type == event_type && 
            g_subscriptions[i].callback == callback) {
            
            g_subscriptions[i].active = FALSE;
            g_subscription_count--;
            hr = TE_S_OK;
            break;
        }
    }
    
    LeaveCriticalSection(&g_event_cs);
    return hr;
}

void TE_EventDispatchFire(uint32_t event_type, const void* event_data)
{
    EnterCriticalSection(&g_event_cs);
    
    /* Make a copy of active callbacks to avoid deadlocks or issues if a callback subscribes/unsubscribes */
    TE_Subscription local_subs[TE_MAX_SUBSCRIPTIONS];
    uint32_t local_count = 0;
    
    for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        if (g_subscriptions[i].active && g_subscriptions[i].event_type == event_type) {
            local_subs[local_count++] = g_subscriptions[i];
        }
    }
    
    LeaveCriticalSection(&g_event_cs);
    
    for (uint32_t i = 0; i < local_count; ++i) {
#ifdef _MSC_VER
        __try {
            local_subs[i].callback(event_type, event_data, local_subs[i].user_data);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            TE_LogWrite(TE_LOG_ERROR, "EventDispatch", "Exception caught in event callback");
        }
#else
        local_subs[i].callback(event_type, event_data, local_subs[i].user_data);
#endif
    }
}

void TE_EventDispatchRemoveByPlugin(uint32_t plugin_id)
{
    EnterCriticalSection(&g_event_cs);
    
    for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        if (g_subscriptions[i].active && g_subscriptions[i].plugin_id == plugin_id) {
            g_subscriptions[i].active = FALSE;
            g_subscription_count--;
        }
    }
    
    LeaveCriticalSection(&g_event_cs);
}

uint32_t TE_EventDispatchGetCount(void)
{
    uint32_t count = 0;
    EnterCriticalSection(&g_event_cs);
    count = g_subscription_count;
    LeaveCriticalSection(&g_event_cs);
    return count;
}
