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


LRESULT CALLBACK TE_TaskbarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    (void)dwRefData;
    
    switch (msg) {
        case WM_TE_INIT:
            SetTimer(hwnd, 1001, 16, NULL);
            TE_CoreManagerInit(hwnd);
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
            break;
        }
        
        case WM_DESTROY:
        case WM_ENDSESSION:
            KillTimer(hwnd, 1001);
            TE_CoreManagerShutdown();
            TE_TaskbarSubclassRemove(hwnd);
            break;
            
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
            break;
            
        default:
            if (msg == TE_ShellHookGetMessageId() && msg != 0) {
                TE_ShellHookProcess(wParam, lParam);
            } else if (TE_IsMessageSubscribed(msg)) {
                /* TODO(Phase3): Forward subscribed messages to plugins */
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
