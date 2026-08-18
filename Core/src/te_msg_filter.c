#include "core/te_msg_filter.h"
#include <windows.h>
#include <string.h>

typedef struct TE_MsgSubscription {
    uint32_t plugin_id;
    UINT     win_msg;
} TE_MsgSubscription;

typedef struct TE_MsgCount {
    UINT     win_msg;
    uint32_t count;
} TE_MsgCount;

static TE_MsgSubscription g_subscriptions[TE_MAX_MSG_SUBSCRIPTIONS];
static uint32_t g_subscription_count = 0;

static TE_MsgCount g_msg_counts[TE_MAX_MSG_SUBSCRIPTIONS];
static uint32_t g_unique_msg_count = 0;

static SRWLOCK g_msg_filter_lock = SRWLOCK_INIT;

/* Binary search helper */
static int FindMsgCountIndex(UINT win_msg, bool* found)
{
    int left = 0;
    int right = (int)g_unique_msg_count - 1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (g_msg_counts[mid].win_msg == win_msg) {
            if (found) *found = true;
            return mid;
        }
        if (g_msg_counts[mid].win_msg < win_msg) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    if (found) *found = false;
    return left; /* insertion point */
}

static void AddMsgCount(UINT win_msg)
{
    bool found = false;
    int idx = FindMsgCountIndex(win_msg, &found);
    if (found) {
        g_msg_counts[idx].count++;
    } else if (g_unique_msg_count < TE_MAX_MSG_SUBSCRIPTIONS) {
        if (idx < (int)g_unique_msg_count) {
            memmove(&g_msg_counts[idx + 1], &g_msg_counts[idx],
                    (g_unique_msg_count - idx) * sizeof(TE_MsgCount));
        }
        g_msg_counts[idx].win_msg = win_msg;
        g_msg_counts[idx].count = 1;
        g_unique_msg_count++;
    }
}

static void RemoveMsgCount(UINT win_msg)
{
    bool found = false;
    int idx = FindMsgCountIndex(win_msg, &found);
    if (found) {
        g_msg_counts[idx].count--;
        if (g_msg_counts[idx].count == 0) {
            if (idx < (int)g_unique_msg_count - 1) {
                memmove(&g_msg_counts[idx], &g_msg_counts[idx + 1],
                        (g_unique_msg_count - idx - 1) * sizeof(TE_MsgCount));
            }
            g_unique_msg_count--;
        }
    }
}

HRESULT TE_MsgFilterInit(void)
{
    AcquireSRWLockExclusive(&g_msg_filter_lock);
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_subscription_count = 0;
    memset(g_msg_counts, 0, sizeof(g_msg_counts));
    g_unique_msg_count = 0;
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
    
    AddMsgCount(win_msg);

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
            
            RemoveMsgCount(win_msg);
            
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
            UINT msg_to_remove = g_subscriptions[i].win_msg;
            g_subscriptions[i] = g_subscriptions[g_subscription_count - 1];
            memset(&g_subscriptions[g_subscription_count - 1], 0, sizeof(TE_MsgSubscription));
            g_subscription_count--;
            RemoveMsgCount(msg_to_remove);
        } else {
            i++;
        }
    }

    ReleaseSRWLockExclusive(&g_msg_filter_lock);
}

BOOL TE_MsgFilterHasSubscriber(UINT win_msg)
{
    AcquireSRWLockShared(&g_msg_filter_lock);

    bool found = false;
    FindMsgCountIndex(win_msg, &found);

    ReleaseSRWLockShared(&g_msg_filter_lock);
    return found ? TRUE : FALSE;
}

uint32_t TE_MsgFilterGetCount(void)
{
    AcquireSRWLockShared(&g_msg_filter_lock);
    uint32_t count = g_subscription_count;
    ReleaseSRWLockShared(&g_msg_filter_lock);
    return count;
}
