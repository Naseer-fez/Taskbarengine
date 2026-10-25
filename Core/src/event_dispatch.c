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
static SRWLOCK g_event_lock = SRWLOCK_INIT;

HRESULT TE_EventDispatchInit(void)
{
    AcquireSRWLockExclusive(&g_event_lock);
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_subscription_count = 0;
    ReleaseSRWLockExclusive(&g_event_lock);
    return TE_S_OK;
}

void TE_EventDispatchShutdown(void)
{
    AcquireSRWLockExclusive(&g_event_lock);
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_subscription_count = 0;
    ReleaseSRWLockExclusive(&g_event_lock);
}

HRESULT TE_EventDispatchSubscribe(uint32_t event_type, TE_EventCallback callback,
                                   void* user_data, uint32_t plugin_id)
{
    if (!callback) return TE_E_INVALIDARG;
    
    HRESULT hr = TE_E_FAIL;
    AcquireSRWLockExclusive(&g_event_lock);
    
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
    
    ReleaseSRWLockExclusive(&g_event_lock);
    return hr;
}

HRESULT TE_EventDispatchUnsubscribe(uint32_t event_type, TE_EventCallback callback)
{
    if (!callback) return TE_E_INVALIDARG;

    HRESULT hr = TE_E_FAIL;
    AcquireSRWLockExclusive(&g_event_lock);

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
    
    ReleaseSRWLockExclusive(&g_event_lock);
    return hr;
}

void TE_EventDispatchFire(uint32_t event_type, const void* event_data)
{
    AcquireSRWLockShared(&g_event_lock);
    if (g_subscription_count == 0) {
        ReleaseSRWLockShared(&g_event_lock);
        return;
    }
    
    /* Make a copy of active callbacks to avoid deadlocks or issues if a callback subscribes/unsubscribes */
    TE_Subscription local_subs[TE_MAX_SUBSCRIPTIONS];
    uint32_t local_count = 0;
    
    for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        if (g_subscriptions[i].active && g_subscriptions[i].event_type == event_type) {
            local_subs[local_count++] = g_subscriptions[i];
        }
    }
    
    ReleaseSRWLockShared(&g_event_lock);
    
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
    AcquireSRWLockExclusive(&g_event_lock);
    
    for (uint32_t i = 0; i < TE_MAX_SUBSCRIPTIONS; ++i) {
        if (g_subscriptions[i].active && g_subscriptions[i].plugin_id == plugin_id) {
            g_subscriptions[i].active = FALSE;
            g_subscription_count--;
        }
    }
    
    ReleaseSRWLockExclusive(&g_event_lock);
}

uint32_t TE_EventDispatchGetCount(void)
{
    uint32_t count = 0;
    AcquireSRWLockShared(&g_event_lock);
    count = g_subscription_count;
    ReleaseSRWLockShared(&g_event_lock);
    return count;
}
