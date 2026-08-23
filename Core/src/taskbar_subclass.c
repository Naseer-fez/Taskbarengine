#include "core/taskbar_subclass.h"
#include "core/config_watcher.h"
#include "core/core_manager.h"
#include "core/shell_hook.h"
#include "core/power_device.h"
#include "core/te_msg_filter.h"
#include "core/te_timer.h"
#include <commctrl.h>
#include <sdk/te_log.h>
#include <sdk/te_debug_trace.h>
#include <stdio.h>

#ifdef _MSC_VER
#pragma comment(lib, "Comctl32.lib")
#endif

#define TASKBAR_SUBCLASS_ID 0x54455342 /* 'TESB' */

typedef struct SubclassRefData {
    TE_EventEntry* event_table;
    uint32_t* sub_count;
    void* core_state_ptr;
} SubclassRefData;

static SubclassRefData g_subclass_ref = { 0 };
static volatile LONG g_in_geometry_dispatch = 0;

/* TE_CoreManagerOnConfigChanged declared in core/core_manager.h (included above) */

static LRESULT CALLBACK TaskbarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    /* Only log specific messages to avoid flooding */
    if (uMsg == WM_NCDESTROY || uMsg == (WM_APP + 100) || uMsg == (WM_APP + 102) || uMsg == WM_CLOSE || uMsg == WM_DESTROY) {
        char dbg[128]; sprintf(dbg, "[TE-DBG] SubclassProc: msg=0x%04X wp=0x%llX\n", uMsg, (unsigned long long)wParam); TE_DebugTrace(dbg);
    }
    SubclassRefData* ref = (SubclassRefData*)dwRefData;

    /* Process shell hook and power/device messages but let them pass through
     * to DefSubclassProc so Explorer's own handlers still see them.  Swallowing
     * these previously broke Explorer's internal task-list state management. */
    TE_ShellHookHandleMessage(uMsg, wParam, lParam);
    TE_PowerDeviceHandleMessage(uMsg, wParam, lParam);

    switch (uMsg) {
        case WM_SIZE:
        case WM_WINDOWPOSCHANGING: {
            /* Re-entrancy guard: SetWindowPos(SWP_FRAMECHANGED) from plugins
             * generates another WM_WINDOWPOSCHANGING, creating a feedback loop
             * with XAML's own layout engine.  Skip dispatch if already inside. */
            if (InterlockedCompareExchange(&g_in_geometry_dispatch, 1, 0) == 0) {
                if (ref && ref->event_table && ref->sub_count) {
                    RECT rc;
                    GetWindowRect(hWnd, &rc);
                    TE_TaskbarGeometryEvent evt = { 0 };
                    evt.taskbar_hwnd = hWnd;
                    evt.taskbar_rect = rc;
                    evt.window_pos = (uMsg == WM_WINDOWPOSCHANGING) ? (WINDOWPOS*)lParam : NULL;
                    TE_EventDispatch(ref->event_table, *ref->sub_count, TE_EVENT_TASKBAR_GEOMETRY, &evt);
                }
                InterlockedExchange(&g_in_geometry_dispatch, 0);
            }
            break;
        }

        case WM_DISPLAYCHANGE: {
            if (ref && ref->event_table && ref->sub_count) {
                TE_DisplayChangedEvent evt = { 0 };
                evt.monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                GetWindowRect(hWnd, &evt.monitor_rect);
                TE_EventDispatch(ref->event_table, *ref->sub_count, TE_EVENT_DISPLAY_CHANGED, &evt);
            }
            break;
        }

        case WM_DPICHANGED: {
            if (ref && ref->event_table && ref->sub_count) {
                TE_DpiChangedEvent evt = { 0 };
                evt.monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                evt.new_dpi = (uint32_t)LOWORD(wParam);
                TE_EventDispatch(ref->event_table, *ref->sub_count, TE_EVENT_DPI_CHANGED, &evt);
            }
            break;
        }

        case WM_TE_INIT: {
            if (ref && ref->core_state_ptr) {
                TE_LogWrite(TE_LOG_INFO, "Taskbar subclass received WM_TE_INIT, executing deferred init");
                TE_DebugTrace("[TE-DBG] SubclassProc: WM_TE_INIT received, calling PhaseB\n");
                TE_CoreManagerInitPhaseB();
            }
            return 0;
        }

        case WM_TE_CONFIG_CHANGED: {
            TE_LogWrite(TE_LOG_INFO, "Taskbar subclass received WM_TE_CONFIG_CHANGED");
            if (ref && ref->core_state_ptr) {
                TE_CoreManagerOnConfigChanged(ref->core_state_ptr);
            }
            return 0;
        }

        case WM_TE_TIMER_FIRE: {
            TE_TimerDispatchMessage(wParam, lParam);
            return 0;
        }

        case WM_TE_IPC_COMMAND: {
            TE_LogWrite(TE_LOG_INFO, "Taskbar subclass received WM_TE_IPC_COMMAND (cmd=%lu)", (unsigned long)wParam);
            if (wParam == TE_IPC_CMD_RELOAD_CONFIG) {
                TE_CoreManagerReloadConfig();
            } else if (wParam == TE_IPC_CMD_ENABLE_PLUGIN || wParam == TE_IPC_CMD_DISABLE_PLUGIN) {
                const char* name = (const char*)lParam;
                if (name) {
                    return (LRESULT)(LONG)TE_CoreManagerSetPluginEnabledByName(
                        name, wParam == TE_IPC_CMD_ENABLE_PLUGIN);
                }
                return (LRESULT)(LONG)E_INVALIDARG;
            } else if (wParam == TE_IPC_CMD_SHUTDOWN) {
                TE_LogWrite(TE_LOG_INFO, "Taskbar subclass executing TE_IPC_CMD_SHUTDOWN on UI thread");
                TE_CoreManagerShutdownFromIpc();
            } else if (wParam == TE_IPC_CMD_GET_PLUGIN_LIST) {
                TE_IpcSyncPayload* sync = (TE_IpcSyncPayload*)lParam;
                if (sync) {
                    sync->result_code = TE_CoreManagerBuildPluginList((char*)sync->buffer, sync->buffer_len);
                    if (sync->completion_event) {
                        SetEvent(sync->completion_event);
                    }
                }
            } else if (wParam == TE_IPC_CMD_GET_SETTINGS) {
                TE_IpcSyncPayload* sync = (TE_IpcSyncPayload*)lParam;
                if (sync) {
                    sync->result_code = TE_CoreManagerBuildSettingsSchema((char*)sync->buffer, sync->buffer_len);
                    if (sync->completion_event) {
                        SetEvent(sync->completion_event);
                    }
                }
            } else if (wParam == TE_IPC_CMD_GET_PERF_STATS) {
                TE_IpcSyncPayload* sync = (TE_IpcSyncPayload*)lParam;
                if (sync) {
                    sync->result_code = TE_CoreManagerBuildPerfStats((char*)sync->buffer, sync->buffer_len);
                    if (sync->completion_event) {
                        SetEvent(sync->completion_event);
                    }
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (TE_MsgFilterHasSubscriber(WM_MOUSEMOVE)) {
                if (ref && ref->event_table && ref->sub_count) {
                    TE_TaskbarMouseEvent evt = { 0 };
                    evt.taskbar_hwnd = hWnd;
                    evt.x = (int)(short)LOWORD(lParam);
                    evt.y = (int)(short)HIWORD(lParam);
                    TE_EventDispatch(ref->event_table, *ref->sub_count, TE_EVENT_TASKBAR_MOUSE, &evt);
                }
            }
            break;
        }

        case WM_NCDESTROY: {
            TE_DebugTrace("[TE-DBG] SubclassProc: WM_NCDESTROY - Shell_TrayWnd is being DESTROYED!\n");
            RemoveWindowSubclass(hWnd, TaskbarSubclassProc, TASKBAR_SUBCLASS_ID);
            TE_LogWrite(TE_LOG_INFO, "Subclass automatically removed on WM_NCDESTROY, triggering Core Manager shutdown");
            TE_CoreManagerShutdown();
            break;
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

HRESULT TE_TaskbarSubclassInstall(HWND taskbar_hwnd, TE_EventEntry* event_table, uint32_t* sub_count, void* core_state_ptr)
{
    if (!taskbar_hwnd || !IsWindow(taskbar_hwnd)) return E_HANDLE;

    g_subclass_ref.event_table = event_table;
    g_subclass_ref.sub_count = sub_count;
    g_subclass_ref.core_state_ptr = core_state_ptr;

    BOOL ok = SetWindowSubclass(taskbar_hwnd, TaskbarSubclassProc, TASKBAR_SUBCLASS_ID, (DWORD_PTR)&g_subclass_ref);
    if (!ok) {
        TE_LogWrite(TE_LOG_ERROR, "SetWindowSubclass failed on Shell_TrayWnd");
        return E_FAIL;
    }
    TE_DebugTrace("[TE-DBG] SubclassInstall: SetWindowSubclass succeeded\n");

    TE_LogWrite(TE_LOG_INFO, "Subclassed Shell_TrayWnd successfully");
    return S_OK;
}

void TE_TaskbarSubclassRemove(HWND taskbar_hwnd)
{
    if (taskbar_hwnd && IsWindow(taskbar_hwnd)) {
        RemoveWindowSubclass(taskbar_hwnd, TaskbarSubclassProc, TASKBAR_SUBCLASS_ID);
        TE_LogWrite(TE_LOG_INFO, "Removed subclass from Shell_TrayWnd");
    }
}
