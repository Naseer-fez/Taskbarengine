#include "tray_discovery.h"
#include <string.h>
#include <stdio.h>

static TE_TrayDiscoveryState* s_pActiveState = NULL;

static void CALLBACK CompositionBridgeEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                             LONG object_id, LONG child_id, DWORD event_thread,
                                             DWORD event_time) {
    (void)hook; (void)event; (void)event_thread; (void)event_time;
    if (!s_pActiveState || object_id != OBJID_WINDOW || child_id != CHILDID_SELF || !hwnd) return;
    wchar_t class_name[128] = {0};
    if (!GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0]))) ||
        wcscmp(class_name, L"Windows.UI.Composition.DesktopWindowContentBridge") != 0) return;
    TE_TrayHandleDisplayChange(s_pActiveState, &s_pActiveState->current_policy);
}

typedef struct TE_BridgeSearch {
    HWND bridge;
    DWORD owner_pid;
} TE_BridgeSearch;

static BOOL CALLBACK FindCompositionBridge(HWND hwnd, LPARAM parameter) {
    TE_BridgeSearch* search = (TE_BridgeSearch*)parameter;
    wchar_t class_name[128] = {0};
    DWORD pid = 0;
    if (!search || !GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])))) return TRUE;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == search->owner_pid && wcscmp(class_name, L"Windows.UI.Composition.DesktopWindowContentBridge") == 0) {
        search->bridge = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND FindTaskbarCompositionBridge(HWND taskbar) {
    DWORD owner_pid = 0;
    GetWindowThreadProcessId(taskbar, &owner_pid);
    TE_BridgeSearch search = { NULL, owner_pid };
    EnumChildWindows(taskbar, FindCompositionBridge, (LPARAM)&search);
    return search.bridge;
}

static void LogWindowResult(const char* operation, HWND hwnd, bool ok) {
    wchar_t class_name[128] = L"?";
    DWORD pid = 0;
    DWORD tid = 0;
    if (hwnd && IsWindow(hwnd)) {
        GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])));
        tid = GetWindowThreadProcessId(hwnd, &pid);
    }
    char class_utf8[128] = "?";
    WideCharToMultiByte(CP_UTF8, 0, class_name, -1, class_utf8, sizeof(class_utf8), NULL, NULL);
    char message[320];
    const DWORD error = ok ? ERROR_SUCCESS : TE_CompositionGetLastError();
    sprintf_s(message, sizeof(message), "%s HWND=%p class=%s pid=%lu tid=%lu result=%s GetLastError=%lu",
              operation, (void*)hwnd, class_utf8, pid, tid, ok ? "TRUE" : "FALSE", error);
    strcat_s(message, sizeof(message), "\n");
    OutputDebugStringA(message);
}

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
    memset(&state->taskbars[state->count], 0, sizeof(state->taskbars[state->count]));
    state->taskbars[state->count].hwnd = hwnd;
    state->taskbars[state->count].composition_hwnd = FindTaskbarCompositionBridge(hwnd);
    state->taskbars[state->count].monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    state->taskbars[state->count].is_primary = is_primary;
    state->taskbars[state->count].policy_applied = false;
    state->count++;
    return true;
}

uint32_t TE_TrayDiscoverAll(TE_TrayDiscoveryState* state) {
    if (!state) return 0;
    state->count = 0;
    memset(state->taskbars, 0, sizeof(state->taskbars));

    DWORD current_pid = GetCurrentProcessId();

    /* 1. Primary taskbar */
    HWND primary = FindWindowW(L"Shell_TrayWnd", NULL);
    DWORD primary_pid = 0;
    if (primary && IsWindow(primary)) {
        GetWindowThreadProcessId(primary, &primary_pid);
        state->taskbars[state->count].hwnd = primary;
        state->taskbars[state->count].composition_hwnd = FindTaskbarCompositionBridge(primary);
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
            state->taskbars[state->count].composition_hwnd = FindTaskbarCompositionBridge(sec);
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
            state->taskbars[i].policy_last_error = TE_CompositionGetLastError();
            LogWindowResult("SetWindowCompositionAttribute(root)", state->taskbars[i].hwnd, ok);
            HWND bridge = state->taskbars[i].composition_hwnd;
            if (bridge && IsWindow(bridge)) {
                const bool bridge_ok = TE_ApplyAccentPolicy(bridge, policy);
                state->taskbars[i].composition_policy_applied = bridge_ok;
                state->taskbars[i].composition_last_error = TE_CompositionGetLastError();
                LogWindowResult("SetWindowCompositionAttribute(bridge)", bridge, bridge_ok);
            } else {
                state->taskbars[i].composition_policy_applied = false;
                state->taskbars[i].composition_last_error = ERROR_NOT_FOUND;
                OutputDebugStringA("TaskbarTransparency: no in-process DesktopWindowContentBridge descendant found\n");
            }
        }
    }
}

void TE_TrayRestoreAll(TE_TrayDiscoveryState* state) {
    if (!state) return;

    TE_TrayWatchCompositionBridge(state, false);

    if (state->debounce_timer_id != 0) {
        KillTimer(NULL, state->debounce_timer_id);
        state->debounce_timer_id = 0;
    }

    if (s_pActiveState == state) {
        s_pActiveState = NULL;
    }

    for (uint32_t i = 0; i < state->count; i++) {
        if (state->taskbars[i].hwnd && IsWindow(state->taskbars[i].hwnd)) {
            const bool root_restored = TE_RestoreNativeAccent(state->taskbars[i].hwnd);
            LogWindowResult("RestoreAccent(root)", state->taskbars[i].hwnd, root_restored);
            state->taskbars[i].policy_applied = false;
            HWND bridge = state->taskbars[i].composition_hwnd;
            if (bridge && IsWindow(bridge)) {
                const bool restored = TE_RestoreNativeAccent(bridge);
                state->taskbars[i].composition_policy_applied = false;
                LogWindowResult("RestoreAccent(bridge)", bridge, restored && TE_CompositionGetLastError() == ERROR_SUCCESS);
            }
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

    SetLastError(ERROR_SUCCESS);
    state->debounce_timer_id = SetTimer(NULL, 0, 150, DebounceTimerProc);
    if (!state->debounce_timer_id) OutputDebugStringA("TaskbarTransparency: SetTimer(rediscovery) failed\n");
}

bool TE_TrayWatchCompositionBridge(TE_TrayDiscoveryState* state, bool enable) {
    if (!state) return false;
    if (!enable) {
        if (state->composition_bridge_hook) {
            UnhookWinEvent(state->composition_bridge_hook);
            state->composition_bridge_hook = NULL;
        }
        return true;
    }
    if (state->composition_bridge_hook) return true;
    s_pActiveState = state;
    state->composition_bridge_hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_DESTROY,
        NULL, CompositionBridgeEvent, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    return state->composition_bridge_hook != NULL;
}

void TE_TrayCleanup(TE_TrayDiscoveryState* state) {
    if (!state) return;

    if (s_pActiveState == state) {
        s_pActiveState = NULL;
    }

    TE_TrayWatchCompositionBridge(state, false);

    if (state->debounce_timer_id != 0) {
        KillTimer(NULL, state->debounce_timer_id);
        state->debounce_timer_id = 0;
    }

    TE_TrayRestoreAll(state);
    state->count = 0;
}
