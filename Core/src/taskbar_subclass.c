#include "core/taskbar_subclass.h"
#include <windows.h>
#include <commctrl.h>
#include "core/core_manager.h"
#include "core/event_dispatch.h"
#include "core/shell_hook.h"
#include "core/power_device.h"
#include <sdk/te_events.h>
#include <sdk/te_log.h>

static UINT g_subscribed_messages[32];
static int g_subscribed_count = 0;

static BOOL TE_IsMessageSubscribed(UINT msg) {
    for (int i = 0; i < g_subscribed_count; i++) {
        if (g_subscribed_messages[i] == msg) return TRUE;
    }
    return FALSE;
}

HRESULT TE_TaskbarSubclassSubscribeMessage(UINT msg) {
    if (TE_IsMessageSubscribed(msg)) return TE_S_OK;
    if (g_subscribed_count >= 32) return TE_E_FAIL;
    g_subscribed_messages[g_subscribed_count++] = msg;
    return TE_S_OK;
}

HRESULT TE_TaskbarSubclassUnsubscribeMessage(UINT msg) {
    for (int i = 0; i < g_subscribed_count; i++) {
        if (g_subscribed_messages[i] == msg) {
            g_subscribed_messages[i] = g_subscribed_messages[g_subscribed_count - 1];
            g_subscribed_count--;
            return TE_S_OK;
        }
    }
    return TE_S_OK;
}

static TE_TrackedTaskbar s_tracked_taskbars[TE_MAX_TRACKED_TASKBARS] = { 0 };
static uint32_t s_tracked_count = 0;
static SRWLOCK s_track_lock = SRWLOCK_INIT;

uint32_t TE_TaskbarGetTrackedCount(void) {
    AcquireSRWLockShared(&s_track_lock);
    uint32_t count = s_tracked_count;
    ReleaseSRWLockShared(&s_track_lock);
    return count;
}

BOOL TE_TaskbarGetTracked(uint32_t index, TE_TrackedTaskbar* out_taskbar) {
    if (!out_taskbar) return FALSE;
    AcquireSRWLockShared(&s_track_lock);
    if (index >= s_tracked_count) {
        ReleaseSRWLockShared(&s_track_lock);
        return FALSE;
    }
    *out_taskbar = s_tracked_taskbars[index];
    ReleaseSRWLockShared(&s_track_lock);
    return TRUE;
}

BOOL TE_TaskbarIsTracked(HWND hwnd) {
    if (!hwnd) return FALSE;
    AcquireSRWLockShared(&s_track_lock);
    for (uint32_t i = 0; i < s_tracked_count; i++) {
        if (s_tracked_taskbars[i].hwnd == hwnd) {
            ReleaseSRWLockShared(&s_track_lock);
            return TRUE;
        }
    }
    ReleaseSRWLockShared(&s_track_lock);
    return FALSE;
}

HWND TE_TaskbarFindRoot(HWND hwnd) {
    if (!hwnd) return NULL;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root) root = hwnd;

    AcquireSRWLockShared(&s_track_lock);
    for (uint32_t i = 0; i < s_tracked_count; i++) {
        if (s_tracked_taskbars[i].hwnd == root || s_tracked_taskbars[i].hwnd == hwnd) {
            HWND found = s_tracked_taskbars[i].hwnd;
            ReleaseSRWLockShared(&s_track_lock);
            return found;
        }
    }
    ReleaseSRWLockShared(&s_track_lock);
    return NULL;
}

static BOOL AddTrackedInternal(HWND hwnd, BOOL is_primary) {
    if (!hwnd || !IsWindow(hwnd)) return FALSE;
    for (uint32_t i = 0; i < s_tracked_count; i++) {
        if (s_tracked_taskbars[i].hwnd == hwnd) {
            s_tracked_taskbars[i].is_primary = is_primary;
            s_tracked_taskbars[i].monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            return TRUE;
        }
    }
    if (s_tracked_count >= TE_MAX_TRACKED_TASKBARS) return FALSE;

    s_tracked_taskbars[s_tracked_count].hwnd = hwnd;
    s_tracked_taskbars[s_tracked_count].monitor = MonitorFromWindow(hwnd, is_primary ? MONITOR_DEFAULTTOPRIMARY : MONITOR_DEFAULTTONEAREST);
    s_tracked_taskbars[s_tracked_count].is_primary = is_primary;
    s_tracked_count++;
    return TRUE;
}

uint32_t TE_TaskbarTrackAll(void) {
    AcquireSRWLockExclusive(&s_track_lock);

    /* 1. Prune dead taskbar windows */
    uint32_t alive = 0;
    for (uint32_t i = 0; i < s_tracked_count; i++) {
        if (IsWindow(s_tracked_taskbars[i].hwnd)) {
            if (alive != i) {
                s_tracked_taskbars[alive] = s_tracked_taskbars[i];
            }
            alive++;
        }
    }
    s_tracked_count = alive;

    DWORD cur_pid = GetCurrentProcessId();

    /* 2. Track primary taskbar */
    HWND primary = FindWindowW(L"Shell_TrayWnd", NULL);
    if (primary && IsWindow(primary)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(primary, &pid);
        if (pid == cur_pid || cur_pid == 0) {
            BOOL was_present = FALSE;
            for (uint32_t i = 0; i < s_tracked_count; i++) {
                if (s_tracked_taskbars[i].hwnd == primary) {
                    was_present = TRUE;
                    break;
                }
            }
            AddTrackedInternal(primary, TRUE);
            if (!was_present) {
                TE_TaskbarSubclassInstall(primary);
            }
        }
    }

    /* 3. Track all secondary taskbars */
    HWND sec = NULL;
    while ((sec = FindWindowExW(NULL, sec, L"Shell_SecondaryTrayWnd", NULL)) != NULL) {
        if (!IsWindow(sec)) continue;
        DWORD pid = 0;
        GetWindowThreadProcessId(sec, &pid);
        if (pid == cur_pid || cur_pid == 0) {
            BOOL was_present = FALSE;
            for (uint32_t i = 0; i < s_tracked_count; i++) {
                if (s_tracked_taskbars[i].hwnd == sec) {
                    was_present = TRUE;
                    break;
                }
            }
            if (AddTrackedInternal(sec, FALSE) && !was_present) {
                TE_TaskbarSubclassInstall(sec);
                SetTimer(sec, 1001, 16, NULL);
            }
        }
    }

    uint32_t result_count = s_tracked_count;
    ReleaseSRWLockExclusive(&s_track_lock);
    return result_count;
}

void TE_TaskbarUntrackAll(void) {
    AcquireSRWLockExclusive(&s_track_lock);
    for (uint32_t i = 0; i < s_tracked_count; i++) {
        if (s_tracked_taskbars[i].hwnd && IsWindow(s_tracked_taskbars[i].hwnd)) {
            KillTimer(s_tracked_taskbars[i].hwnd, 1001);
            TE_TaskbarSubclassRemove(s_tracked_taskbars[i].hwnd);
        }
    }
    s_tracked_count = 0;
    ReleaseSRWLockExclusive(&s_track_lock);
}

static VOID CALLBACK DisplayChangeDebounceProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    (void)hwnd; (void)uMsg; (void)dwTime;
    KillTimer(NULL, idEvent);
    TE_TaskbarTrackAll();
}

LRESULT CALLBACK TE_TaskbarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    (void)dwRefData;
    
    switch (msg) {
        case WM_TE_INIT:
            SetTimer(hwnd, 1001, 16, NULL);
            TE_CoreManagerInit(hwnd);
            TE_TaskbarTrackAll();
            return 0;
            
        case WM_TE_IPC_COMMAND:
            if ((int)wParam == TE_CMD_SHUTDOWN) {
                KillTimer(hwnd, 1001);
                TE_CoreManagerHandleCommand((int)wParam, (void*)lParam);
                TE_TaskbarSubclassRemove(hwnd);
                return 0;
            }
            TE_CoreManagerHandleCommand((int)wParam, (void*)lParam);
            return 0;
            
        case WM_DPICHANGED: {
            TE_DpiChangedData data;
            data.old_dpi = TE_CoreManagerGetDpi();
            data.new_dpi = HIWORD(wParam);
            data.hwnd = hwnd;
            TE_CoreManagerSetDpi(data.new_dpi);
            TE_EventDispatchFire(TE_EVENT_DPI_CHANGED, &data);
            break;
        }
        
        case WM_DISPLAYCHANGE: {
            TE_DisplayChangedData data;
            data.width = LOWORD(lParam);
            data.height = HIWORD(lParam);
            data.bits_per_pixel = (uint32_t)wParam;
            TE_EventDispatchFire(TE_EVENT_DISPLAY_CHANGED, &data);

            /* Immediate track and debounced check for secondary trays */
            TE_TaskbarTrackAll();
            SetTimer(NULL, 0, 150, DisplayChangeDebounceProc);
            break;
        }
        
        case WM_DESTROY:
        case WM_ENDSESSION: {
            KillTimer(hwnd, 1001);
            HWND primary_tb = TE_CoreManagerGetTaskbarHwnd();
            if (hwnd == primary_tb) {
                TE_CoreManagerShutdown();
                TE_TaskbarSubclassRemove(hwnd);
            } else {
                TE_TaskbarSubclassRemove(hwnd);
                AcquireSRWLockExclusive(&s_track_lock);
                for (uint32_t i = 0; i < s_tracked_count; i++) {
                    if (s_tracked_taskbars[i].hwnd == hwnd) {
                        for (uint32_t j = i; j + 1 < s_tracked_count; j++) {
                            s_tracked_taskbars[j] = s_tracked_taskbars[j + 1];
                        }
                        s_tracked_count--;
                        break;
                    }
                }
                ReleaseSRWLockExclusive(&s_track_lock);
            }
            break;
        }
            
        case WM_TIMER: {
            if (wParam == 1001) {
                POINT pt;
                GetCursorPos(&pt);
                RECT rect;
                GetWindowRect(hwnd, &rect);
                
                BOOL in_taskbar = FALSE;
                if (PtInRect(&rect, pt)) {
                    HWND hit_hwnd = WindowFromPoint(pt);
                    if (hit_hwnd) {
                        HWND root = GetAncestor(hit_hwnd, GA_ROOT);
                        if (root == hwnd) {
                            in_taskbar = TRUE;
                        }
                    }
                }
                
                static BOOL s_was_in_taskbar = FALSE;
                static BOOL s_was_dragging = FALSE;
                static POINT s_last_pt = {0, 0};
                BOOL is_dragging = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
                
                if (in_taskbar || s_was_in_taskbar) {
                    if (pt.x != s_last_pt.x || pt.y != s_last_pt.y || in_taskbar != s_was_in_taskbar || is_dragging != s_was_dragging) {
                        TE_TaskbarMouseData mouse_data;
                        mouse_data.cursor_pos = pt;
                        mouse_data.is_in_taskbar = in_taskbar;
                        mouse_data.is_dragging = is_dragging;
                        mouse_data.taskbar_hwnd = hwnd;
                        TE_EventDispatchFire(TE_EVENT_TASKBAR_MOUSE, &mouse_data);
                        
                        s_last_pt = pt;
                        s_was_in_taskbar = in_taskbar;
                        s_was_dragging = is_dragging;
                    }
                }
            }
            break;
        }

        case WM_WINDOWPOSCHANGED: {
            const WINDOWPOS* wp = (const WINDOWPOS*)lParam;
            if (wp && (!(wp->flags & SWP_NOMOVE) || !(wp->flags & SWP_NOSIZE))) {
                TE_TaskbarGeometryData geom;
                GetWindowRect(hwnd, &geom.new_rect);
                geom.monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                TE_EventDispatchFire(TE_EVENT_TASKBAR_GEOMETRY, &geom);
            }
            break;
        }

        case WM_POWERBROADCAST:
            TE_PowerProcess(wParam, lParam);
            break;

        case WM_DEVICECHANGE:
            TE_DeviceProcess(wParam, lParam);
            TE_TaskbarTrackAll();
            break;
            
        default:
            if (msg == TE_ShellHookGetMessageId() && msg != 0) {
                TE_ShellHookProcess(wParam, lParam);
            } else if (TE_IsMessageSubscribed(msg)) {
                /* Forward subscribed messages to plugins */
            }
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

HRESULT TE_TaskbarSubclassInstall(HWND taskbar_hwnd) {
    if (!taskbar_hwnd) return TE_E_INVALIDARG;
    SetWindowSubclass(taskbar_hwnd, TE_TaskbarSubclassProc, TE_SUBCLASS_ID, 0);
    return TE_S_OK;
}

void TE_TaskbarSubclassRemove(HWND taskbar_hwnd) {
    if (!taskbar_hwnd) return;
    RemoveWindowSubclass(taskbar_hwnd, TE_TaskbarSubclassProc, TE_SUBCLASS_ID);
}
