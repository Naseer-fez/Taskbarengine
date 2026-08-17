#include "core/te_msg_filter.h"
#include <windows.h>
#include <string.h>

typedef struct TE_MsgSubscription {
    uint32_t plugin_id;
    UINT     win_msg;
} TE_MsgSubscription;

static TE_MsgSubscription g_subscriptions[TE_MAX_MSG_SUBSCRIPTIONS];
static uint32_t g_subscription_count = 0;
static SRWLOCK g_msg_filter_lock = SRWLOCK_INIT;

HRESULT TE_MsgFilterInit(void)
{
    AcquireSRWLockExclusive(&g_msg_filter_lock);
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_subscription_count = 0;
    ReleaseSRWLockExclusive(&g_msg_filter_lock);
    return S_OK;
}

HRESULT TE_MsgFilterSubscribe(uint32_t plugin_id, UINT win_msg)
{
    if (plugin_id == 0) {
        return E_INVALIDARG;
    }

    AcquireSRWLockExclusive(&g_msg_filter_lock);

    /* Check for duplicate subscription */
    for (uint32_t i = 0; i < g_subscription_count; i++) {
        if (g_subscriptions[i].plugin_id == plugin_id && g_subscriptions[i].win_msg == win_msg) {
            ReleaseSRWLockExclusive(&g_msg_filter_lock);
            return S_OK;
        }
    }

    if (g_subscription_count >= TE_MAX_MSG_SUBSCRIPTIONS) {
        ReleaseSRWLockExclusive(&g_msg_filter_lock);
        return E_OUTOFMEMORY;
    }

    g_subscriptions[g_subscription_count].plugin_id = plugin_id;
    g_subscriptions[g_subscription_count].win_msg = win_msg;
    g_subscription_count++;

    ReleaseSRWLockExclusive(&g_msg_filter_lock);
    return S_OK;
}

HRESULT TE_MsgFilterUnsubscribe(uint32_t plugin_id, UINT win_msg)
{
    if (plugin_id == 0) {
        return E_INVALIDARG;
    }

    AcquireSRWLockExclusive(&g_msg_filter_lock);

    for (uint32_t i = 0; i < g_subscription_count; i++) {
        if (g_subscriptions[i].plugin_id == plugin_id && g_subscriptions[i].win_msg == win_msg) {
            /* Compact array by moving last element to current position */
            g_subscriptions[i] = g_subscriptions[g_subscription_count - 1];
            memset(&g_subscriptions[g_subscription_count - 1], 0, sizeof(TE_MsgSubscription));
            g_subscription_count--;
            ReleaseSRWLockExclusive(&g_msg_filter_lock);
            return S_OK;
        }
    }

    ReleaseSRWLockExclusive(&g_msg_filter_lock);
    return S_FALSE;
}

void TE_MsgFilterUnsubscribeAll(uint32_t plugin_id)
{
    if (plugin_id == 0) {
        return;
    }

    AcquireSRWLockExclusive(&g_msg_filter_lock);

    uint32_t i = 0;
    while (i < g_subscription_count) {
        if (g_subscriptions[i].plugin_id == plugin_id) {
            g_subscriptions[i] = g_subscriptions[g_subscription_count - 1];
            memset(&g_subscriptions[g_subscription_count - 1], 0, sizeof(TE_MsgSubscription));
            g_subscription_count--;
        } else {
            i++;
        }
    }

    ReleaseSRWLockExclusive(&g_msg_filter_lock);
}

BOOL TE_MsgFilterHasSubscriber(UINT win_msg)
{
    AcquireSRWLockShared(&g_msg_filter_lock);

    for (uint32_t i = 0; i < g_subscription_count; i++) {
        if (g_subscriptions[i].win_msg == win_msg) {
            ReleaseSRWLockShared(&g_msg_filter_lock);
            return TRUE;
        }
    }

    ReleaseSRWLockShared(&g_msg_filter_lock);
    return FALSE;
}

uint32_t TE_MsgFilterGetCount(void)
{
    AcquireSRWLockShared(&g_msg_filter_lock);
    uint32_t count = g_subscription_count;
    ReleaseSRWLockShared(&g_msg_filter_lock);
    return count;
}
