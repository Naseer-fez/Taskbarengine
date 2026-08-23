#include "core/te_timer.h"
#include <windows.h>
#include <string.h>

#define TE_MAX_TIMERS 64

typedef struct TE_TimerEntry {
    uint32_t         timer_id;
    HANDLE           timer_handle;
    TE_TimerCallback callback;
    void*            user_data;
    uint32_t         interval_ms;
    BOOL             recurring;
    uint32_t         owner_plugin_id;
    volatile LONG    cancelled;
    volatile LONG    in_use;
    uint32_t         fire_count;
} TE_TimerEntry;

static TE_TimerEntry g_timers[TE_MAX_TIMERS];
static HANDLE g_timer_queue = NULL;
static HWND g_notify_hwnd = NULL;
static SRWLOCK g_timer_lock = SRWLOCK_INIT;
static volatile LONG g_timer_seq = 1;
static volatile LONG g_timer_stopping = 0;

static VOID CALLBACK TimerQueueRoutine(PVOID param, BOOLEAN timer_or_wait_fired)
{
    (void)timer_or_wait_fired;
    if (InterlockedCompareExchange(&g_timer_stopping, 0, 0)) return;

    uint32_t timer_id = (uint32_t)(uintptr_t)param;

    AcquireSRWLockShared(&g_timer_lock);

    TE_TimerEntry* target = NULL;
    for (int i = 0; i < TE_MAX_TIMERS; i++) {
        if (g_timers[i].in_use && g_timers[i].timer_id == timer_id && !g_timers[i].cancelled) {
            target = &g_timers[i];
            break;
        }
    }

    if (!target) {
        ReleaseSRWLockShared(&g_timer_lock);
        return;
    }

    HWND hwnd = g_notify_hwnd;
    ReleaseSRWLockShared(&g_timer_lock);

    if (hwnd && IsWindow(hwnd)) {
        PostMessageW(hwnd, WM_TE_TIMER_FIRE, (WPARAM)timer_id, 0);
    } else {
        /* If no UI window is configured (e.g. unit test runner), execute on thread pool directly */
        AcquireSRWLockExclusive(&g_timer_lock);
        if (target->in_use && target->timer_id == timer_id && !target->cancelled && target->callback) {
            TE_TimerCallback cb = target->callback;
            void* udata = target->user_data;
            BOOL rec = target->recurring;
            target->fire_count++;

            if (!rec) {
                target->cancelled = 1;
                HANDLE th = target->timer_handle;
                target->timer_handle = NULL;
                if (th && g_timer_queue) {
                    DeleteTimerQueueTimer(g_timer_queue, th, NULL);
                }
                /* We do not clear in_use here, letting the client cancel or it remains tombstoned. 
                   Wait, actually for one-shot we should clear it. */
                target->in_use = 0;
            }

            ReleaseSRWLockExclusive(&g_timer_lock);
            cb(udata);
        } else {
            ReleaseSRWLockExclusive(&g_timer_lock);
        }
    }
}

HRESULT TE_TimerInit(HWND notify_hwnd)
{
    AcquireSRWLockExclusive(&g_timer_lock);
    InterlockedExchange(&g_timer_stopping, 1);
    HANDLE saved_queue = g_timer_queue;
    g_timer_queue = NULL;

    for (int i = 0; i < TE_MAX_TIMERS; i++) {
        if (g_timers[i].in_use) {
            g_timers[i].cancelled = 1;
            if (g_timers[i].timer_handle && saved_queue) {
                DeleteTimerQueueTimer(saved_queue, g_timers[i].timer_handle, NULL);
                g_timers[i].timer_handle = NULL;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_timer_lock);

    if (saved_queue) {
        DeleteTimerQueueEx(saved_queue, INVALID_HANDLE_VALUE);
    }

    AcquireSRWLockExclusive(&g_timer_lock);
    memset(g_timers, 0, sizeof(g_timers));
    g_notify_hwnd = notify_hwnd;

    g_timer_queue = CreateTimerQueue();
    if (!g_timer_queue) {
        InterlockedExchange(&g_timer_stopping, 0);
        ReleaseSRWLockExclusive(&g_timer_lock);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    InterlockedExchange(&g_timer_stopping, 0);
    ReleaseSRWLockExclusive(&g_timer_lock);
    return S_OK;
}

HRESULT TE_TimerCreate(TE_TimerCallback callback, void* user_data,
                       uint32_t interval_ms, BOOL recurring,
                       uint32_t owner_plugin_id, uint32_t* out_timer_id)
{
    if (!callback || interval_ms == 0) {
        return E_INVALIDARG;
    }

    AcquireSRWLockExclusive(&g_timer_lock);

    if (InterlockedCompareExchange(&g_timer_stopping, 0, 0)) {
        ReleaseSRWLockExclusive(&g_timer_lock);
        return E_FAIL;
    }

    if (!g_timer_queue) {
        g_timer_queue = CreateTimerQueue();
        if (!g_timer_queue) {
            ReleaseSRWLockExclusive(&g_timer_lock);
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    int free_slot = -1;
    for (int i = 0; i < TE_MAX_TIMERS; i++) {
        if (!g_timers[i].in_use) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        ReleaseSRWLockExclusive(&g_timer_lock);
        return E_OUTOFMEMORY;
    }

    uint32_t assigned_id = (uint32_t)InterlockedIncrement(&g_timer_seq);

    TE_TimerEntry* entry = &g_timers[free_slot];
    entry->timer_id = assigned_id;
    entry->callback = callback;
    entry->user_data = user_data;
    entry->interval_ms = interval_ms;
    entry->recurring = recurring;
    entry->owner_plugin_id = owner_plugin_id;
    entry->cancelled = 0;
    entry->in_use = 1;
    entry->fire_count = 0;
    entry->timer_handle = NULL;

    DWORD period = recurring ? interval_ms : 0;
    BOOL created = CreateTimerQueueTimer(
        &entry->timer_handle,
        g_timer_queue,
        TimerQueueRoutine,
        (PVOID)(uintptr_t)assigned_id,
        interval_ms,
        period,
        WT_EXECUTEDEFAULT);

    if (!created) {
        DWORD err = GetLastError();
        memset(entry, 0, sizeof(TE_TimerEntry));
        ReleaseSRWLockExclusive(&g_timer_lock);
        return HRESULT_FROM_WIN32(err);
    }

    if (out_timer_id) {
        *out_timer_id = assigned_id;
    }

    ReleaseSRWLockExclusive(&g_timer_lock);
    return S_OK;
}

HRESULT TE_TimerCancelById(uint32_t timer_id)
{
    if (timer_id == 0) {
        return E_INVALIDARG;
    }

    AcquireSRWLockExclusive(&g_timer_lock);

    for (int i = 0; i < TE_MAX_TIMERS; i++) {
        if (g_timers[i].in_use && g_timers[i].timer_id == timer_id) {
            g_timers[i].cancelled = 1;
            g_timers[i].in_use = 0;
            HANDLE th = g_timers[i].timer_handle;
            g_timers[i].timer_handle = NULL;

            if (th && g_timer_queue) {
                DeleteTimerQueueTimer(g_timer_queue, th, NULL);
            }

            memset(&g_timers[i], 0, sizeof(TE_TimerEntry));
            ReleaseSRWLockExclusive(&g_timer_lock);
            return S_OK;
        }
    }

    ReleaseSRWLockExclusive(&g_timer_lock);
    return S_FALSE;
}

HRESULT TE_TimerCancelByCallback(TE_TimerCallback callback, uint32_t owner_plugin_id)
{
    if (!callback) {
        return E_INVALIDARG;
    }

    AcquireSRWLockExclusive(&g_timer_lock);

    BOOL found = FALSE;
    for (int i = 0; i < TE_MAX_TIMERS; i++) {
        if (g_timers[i].in_use && g_timers[i].callback == callback &&
            (owner_plugin_id == 0 || g_timers[i].owner_plugin_id == owner_plugin_id)) {
            g_timers[i].cancelled = 1;
            g_timers[i].in_use = 0;
            HANDLE th = g_timers[i].timer_handle;
            g_timers[i].timer_handle = NULL;

            if (th && g_timer_queue) {
                DeleteTimerQueueTimer(g_timer_queue, th, NULL);
            }

            memset(&g_timers[i], 0, sizeof(TE_TimerEntry));
            found = TRUE;
        }
    }

    ReleaseSRWLockExclusive(&g_timer_lock);
    return found ? S_OK : S_FALSE;
}

void TE_TimerCancelAllForPlugin(uint32_t plugin_id)
{
    if (plugin_id == 0) {
        return;
    }

    AcquireSRWLockExclusive(&g_timer_lock);

    for (int i = 0; i < TE_MAX_TIMERS; i++) {
        if (g_timers[i].in_use && g_timers[i].owner_plugin_id == plugin_id) {
            g_timers[i].cancelled = 1;
            g_timers[i].in_use = 0;
            HANDLE th = g_timers[i].timer_handle;
            g_timers[i].timer_handle = NULL;

            if (th && g_timer_queue) {
                DeleteTimerQueueTimer(g_timer_queue, th, NULL);
            }

            memset(&g_timers[i], 0, sizeof(TE_TimerEntry));
        }
    }

    ReleaseSRWLockExclusive(&g_timer_lock);
}

void TE_TimerDispatchMessage(WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    uint32_t timer_id = (uint32_t)wParam;
    if (timer_id == 0) return;

    TE_TimerCallback callback = NULL;
    void* user_data = NULL;
    BOOL should_invoke = FALSE;

    AcquireSRWLockExclusive(&g_timer_lock);

    for (int i = 0; i < TE_MAX_TIMERS; i++) {
        if (g_timers[i].in_use && g_timers[i].timer_id == timer_id && !g_timers[i].cancelled) {
            callback = g_timers[i].callback;
            user_data = g_timers[i].user_data;
            g_timers[i].fire_count++;
            should_invoke = TRUE;

            if (!g_timers[i].recurring) {
                g_timers[i].cancelled = 1;
                g_timers[i].in_use = 0;
                HANDLE th = g_timers[i].timer_handle;
                g_timers[i].timer_handle = NULL;
                if (th && g_timer_queue) {
                    DeleteTimerQueueTimer(g_timer_queue, th, NULL);
                }
                memset(&g_timers[i], 0, sizeof(TE_TimerEntry));
            }
            break;
        }
    }

    ReleaseSRWLockExclusive(&g_timer_lock);

    if (should_invoke && callback) {
        callback(user_data);
    }
}

void TE_TimerShutdown(void)
{
    AcquireSRWLockExclusive(&g_timer_lock);
    InterlockedExchange(&g_timer_stopping, 1);
    HANDLE saved_queue = g_timer_queue;
    g_timer_queue = NULL;

    for (int i = 0; i < TE_MAX_TIMERS; i++) {
        if (g_timers[i].in_use) {
            g_timers[i].cancelled = 1;
            /* Do not clear in_use to prevent slot reuse while queue drains */
            if (g_timers[i].timer_handle && saved_queue) {
                DeleteTimerQueueTimer(saved_queue, g_timers[i].timer_handle, NULL);
                g_timers[i].timer_handle = NULL;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_timer_lock);

    if (saved_queue) {
        DeleteTimerQueueEx(saved_queue, INVALID_HANDLE_VALUE);
    }

    AcquireSRWLockExclusive(&g_timer_lock);
    memset(g_timers, 0, sizeof(g_timers));
    g_notify_hwnd = NULL;
    InterlockedExchange(&g_timer_stopping, 0);
    ReleaseSRWLockExclusive(&g_timer_lock);
}

uint32_t TE_TimerGetActiveCount(void)
{
    AcquireSRWLockShared(&g_timer_lock);
    uint32_t count = 0;
    for (int i = 0; i < TE_MAX_TIMERS; i++) {
        if (g_timers[i].in_use && !g_timers[i].cancelled) {
            count++;
        }
    }
    ReleaseSRWLockShared(&g_timer_lock);
    return count;
}
