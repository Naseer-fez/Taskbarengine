#include "core/taskbar_subclass.h"
#include "core/config_watcher.h"
#include "core/core_manager.h"
#include "core/shell_hook.h"
#include "core/power_device.h"
#include <commctrl.h>
#include <sdk/te_log.h>

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

/* TE_CoreManagerOnConfigChanged declared in core/core_manager.h (included above) */

static LRESULT CALLBACK TaskbarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    SubclassRefData* ref = (SubclassRefData*)dwRefData;

    if (TE_ShellHookHandleMessage(uMsg, wParam, lParam) ||
        TE_PowerDeviceHandleMessage(uMsg, wParam, lParam)) {
        return 0;
    }

    switch (uMsg) {
        case WM_SIZE:
        case WM_WINDOWPOSCHANGING: {
            if (ref && ref->event_table && ref->sub_count) {
                RECT rc;
                GetWindowRect(hWnd, &rc);
                TE_TaskbarGeometryEvent evt = { 0 };
                evt.taskbar_hwnd = hWnd;
                evt.taskbar_rect = rc;
                evt.window_pos = (uMsg == WM_WINDOWPOSCHANGING) ? (WINDOWPOS*)lParam : NULL;
                TE_EventDispatch(ref->event_table, *ref->sub_count, TE_EVENT_TASKBAR_GEOMETRY, &evt);
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

        case WM_TE_CONFIG_CHANGED: {
            TE_LogWrite(TE_LOG_INFO, "Taskbar subclass received WM_TE_CONFIG_CHANGED");
            if (ref && ref->core_state_ptr) {
                TE_CoreManagerOnConfigChanged(ref->core_state_ptr);
            }
            return 0;
        }

        case WM_TE_IPC_COMMAND: {
            TE_LogWrite(TE_LOG_INFO, "Taskbar subclass received WM_TE_IPC_COMMAND (cmd=%lu)", (unsigned long)wParam);
            if (wParam == TE_IPC_CMD_RELOAD_CONFIG) {
                TE_CoreManagerReloadConfig();
            } else if (wParam == TE_IPC_CMD_ENABLE_PLUGIN || wParam == TE_IPC_CMD_DISABLE_PLUGIN) {
                char* name = (char*)lParam;
                if (name) {
                    TE_CoreManagerSetPluginEnabledByName(name, wParam == TE_IPC_CMD_ENABLE_PLUGIN);
                    HeapFree(GetProcessHeap(), 0, name);
                }
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
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (ref && ref->event_table && ref->sub_count) {
                TE_TaskbarMouseEvent evt = { 0 };
                evt.taskbar_hwnd = hWnd;
                evt.x = (int)(short)LOWORD(lParam);
                evt.y = (int)(short)HIWORD(lParam);
                TE_EventDispatch(ref->event_table, *ref->sub_count, TE_EVENT_TASKBAR_MOUSE, &evt);
            }
            break;
        }

        case WM_NCDESTROY: {
            RemoveWindowSubclass(hWnd, TaskbarSubclassProc, TASKBAR_SUBCLASS_ID);
            TE_LogWrite(TE_LOG_INFO, "Subclass automatically removed on WM_NCDESTROY");
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
