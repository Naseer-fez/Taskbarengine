#include "core/taskbar_subclass.h"
#include <windows.h>
#include <commctrl.h>
#include "core/core_manager.h"
#include "core/event_dispatch.h"
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
            TE_CoreManagerInit(hwnd);
            return 0;
            
        case WM_TE_IPC_COMMAND:
            TE_CoreManagerHandleCommand((int)wParam, (void*)lParam);
            return 0;
            
        case WM_DPICHANGED: {
            TE_DpiChangedData data;
            data.old_dpi = LOWORD(wParam);
            data.new_dpi = HIWORD(wParam);
            data.hwnd = hwnd;
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
            TE_CoreManagerShutdown();
            break;
            
        default:
            if (TE_IsMessageSubscribed(msg)) {
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
