#include "tray_discovery.h"
#include <string.h>

static TE_TrayDiscoveryState* s_pActiveState = NULL;

static VOID CALLBACK DebounceTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    (void)hwnd;
    (void)uMsg;
    (void)dwTime;
    KillTimer(NULL, idEvent);

    TE_TrayDiscoveryState* pState = s_pActiveState;
    if (pState && pState->debounce_timer_id == idEvent) {
        pState->debounce_timer_id = 0;
        TE_TrayDiscoverAll(pState);
        TE_TrayApplyPolicyToAll(pState, &pState->current_policy);
    }
}

void TE_TrayDiscoveryInit(TE_TrayDiscoveryState* state) {
    if (!state) return;
    if (state->debounce_timer_id != 0) {
        KillTimer(NULL, state->debounce_timer_id);
        state->debounce_timer_id = 0;
    }
    if (s_pActiveState && s_pActiveState != state && s_pActiveState->debounce_timer_id != 0) {
        KillTimer(NULL, s_pActiveState->debounce_timer_id);
        s_pActiveState->debounce_timer_id = 0;
    }
    memset(state, 0, sizeof(TE_TrayDiscoveryState));
    state->apply_secondary = true;
    s_pActiveState = state;
}

bool TE_TrayDiscoveryAddTracked(TE_TrayDiscoveryState* state, HWND hwnd, bool is_primary) {
    if (!state || !hwnd || state->count >= TE_MAX_TRACKED_TASKBARS) {
        return false;
    }
    for (uint32_t i = 0; i < state->count; i++) {
        if (state->taskbars[i].hwnd == hwnd) {
            state->taskbars[i].is_primary = is_primary;
            return true;
        }
    }
    state->taskbars[state->count].hwnd = hwnd;
    state->taskbars[state->count].monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    state->taskbars[state->count].is_primary = is_primary;
    state->taskbars[state->count].policy_applied = false;
    state->count++;
    return true;
}

uint32_t TE_TrayDiscoverAll(TE_TrayDiscoveryState* state) {
    if (!state) return 0;
    state->count = 0;

    DWORD current_pid = GetCurrentProcessId();

    /* 1. Primary taskbar */
    HWND primary = FindWindowW(L"Shell_TrayWnd", NULL);
    DWORD primary_pid = 0;
    if (primary && IsWindow(primary)) {
        GetWindowThreadProcessId(primary, &primary_pid);
        state->taskbars[state->count].hwnd = primary;
        state->taskbars[state->count].monitor = MonitorFromWindow(primary, MONITOR_DEFAULTTOPRIMARY);
        state->taskbars[state->count].is_primary = true;
        state->taskbars[state->count].policy_applied = false;
        state->count++;
    }

    /* 2. Secondary taskbars */
    if (state->apply_secondary) {
        HWND sec = NULL;
        while ((sec = FindWindowExW(NULL, sec, L"Shell_SecondaryTrayWnd", NULL)) != NULL) {
            if (state->count >= TE_MAX_TRACKED_TASKBARS) {
                break;
            }
            if (!IsWindow(sec)) {
                continue;
            }

            DWORD sec_pid = 0;
            GetWindowThreadProcessId(sec, &sec_pid);
            /* Validate process ID matches current process or primary taskbar owner */
            if (sec_pid != current_pid && (primary_pid == 0 || sec_pid != primary_pid)) {
                continue;
            }

            state->taskbars[state->count].hwnd = sec;
            state->taskbars[state->count].monitor = MonitorFromWindow(sec, MONITOR_DEFAULTTONEAREST);
            state->taskbars[state->count].is_primary = false;
            state->taskbars[state->count].policy_applied = false;
            state->count++;
        }
    }

    return state->count;
}

void TE_TrayApplyPolicyToAll(TE_TrayDiscoveryState* state, const ACCENT_POLICY* policy) {
    if (!state || !policy) return;

    state->current_policy = *policy;

    for (uint32_t i = 0; i < state->count; i++) {
        if (!state->apply_secondary && !state->taskbars[i].is_primary) {
            continue;
        }
        if (state->taskbars[i].hwnd && IsWindow(state->taskbars[i].hwnd)) {
            bool ok = TE_ApplyAccentPolicy(state->taskbars[i].hwnd, policy);
            state->taskbars[i].policy_applied = ok;
        }
    }
}

void TE_TrayRestoreAll(TE_TrayDiscoveryState* state) {
    if (!state) return;

    if (state->debounce_timer_id != 0) {
        KillTimer(NULL, state->debounce_timer_id);
        state->debounce_timer_id = 0;
    }

    if (s_pActiveState == state) {
        s_pActiveState = NULL;
    }

    for (uint32_t i = 0; i < state->count; i++) {
        if (state->taskbars[i].hwnd && IsWindow(state->taskbars[i].hwnd)) {
            TE_RestoreNativeAccent(state->taskbars[i].hwnd);
            state->taskbars[i].policy_applied = false;
        }
    }
}

void TE_TrayHandleDisplayChange(TE_TrayDiscoveryState* state, const ACCENT_POLICY* policy) {
    if (!state || !policy) return;

    state->current_policy = *policy;
    s_pActiveState = state;

    /* 1. Immediate application to existing taskbar instances */
    TE_TrayDiscoverAll(state);
    TE_TrayApplyPolicyToAll(state, policy);

    /* 2. Coalesce / debounce re-application after Explorer finishes recreating secondary tray windows */
    if (state->debounce_timer_id != 0) {
        KillTimer(NULL, state->debounce_timer_id);
        state->debounce_timer_id = 0;
    }

    state->debounce_timer_id = SetTimer(NULL, 0, 150, DebounceTimerProc);
}

void TE_TrayCleanup(TE_TrayDiscoveryState* state) {
    if (!state) return;

    if (s_pActiveState == state) {
        s_pActiveState = NULL;
    }

    if (state->debounce_timer_id != 0) {
        KillTimer(NULL, state->debounce_timer_id);
        state->debounce_timer_id = 0;
    }

    TE_TrayRestoreAll(state);
    state->count = 0;
}
