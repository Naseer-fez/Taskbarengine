#include "core/engine.h"
#include "core/engine_init.h"
#include "core/taskbar_subclass.h"
#include "core/event_dispatch.h"

#include <windows.h>
#include <commctrl.h>

#include <sdk/te_types.h>
#include <sdk/te_events.h>

static HINSTANCE g_hinstDLL = NULL;
static HWND g_taskbarHwnd = NULL;

BOOL TE_IsExplorerProcess(void)
{
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0) return FALSE;
    
    const wchar_t* filename = wcsrchr(path, L'\\');
    filename = filename ? filename + 1 : path;
    
    return (_wcsicmp(filename, L"explorer.exe") == 0);
}

#include <stdio.h>

static void TE_WriteStartupError(HINSTANCE hInst, const char* msg) {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(hInst, path, MAX_PATH)) {
        wchar_t* last_slash = wcsrchr(path, L'\\');
        if (last_slash) {
            *(last_slash + 1) = L'\0';
            wcscat_s(path, MAX_PATH, L"startup_error.log");
            FILE* f = _wfopen(path, L"a");
            if (f) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
                fclose(f);
            }
        }
    }
}

static volatile LONG g_is_detaching = 0;
static volatile LONG g_is_initialized = 0;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hinstDLL);
            g_hinstDLL = hinstDLL;

            if (!TE_IsExplorerProcess()) {
                return TRUE;
            }
            break;
        }
        case DLL_PROCESS_DETACH:
        {
            /* Invariant (SYS-002): Never acquire user locks, wait on synchronization
             * primitives (WaitForSingleObject), or signal threads from inside DllMain.
             * Module shutdown and thread termination must be triggered asynchronously
             * from the UI message loop prior to uninjection. */
            InterlockedExchange(&g_is_detaching, 1);
            g_taskbarHwnd = NULL;
            break;
        }
    }
    return TRUE;
}

LRESULT CALLBACK TE_GetMsgHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    /* WH_GETMESSAGE hook proc: called on Explorer's message loop */
    if (nCode >= 0 && lParam) {
        const MSG* msg = (const MSG*)lParam;
        
        if (InterlockedCompareExchange(&g_is_initialized, 1, 0) == 0) {
            HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
            if (taskbar_hwnd) {
                g_taskbarHwnd = taskbar_hwnd;
                TE_TaskbarSubclassInstall(taskbar_hwnd);
                PostMessage(taskbar_hwnd, WM_TE_INIT, 0, 0);
            } else {
                InterlockedExchange(&g_is_initialized, 0);
            }
        }

        if (msg->message == WM_MOUSEMOVE && g_taskbarHwnd) {
            static POINT s_last_hook_pt = { -999999, -999999 };
            if (msg->pt.x != s_last_hook_pt.x || msg->pt.y != s_last_hook_pt.y) {
                HWND target_root = TE_TaskbarFindRoot(msg->hwnd);
                if (!target_root && (msg->hwnd == g_taskbarHwnd || IsChild(g_taskbarHwnd, msg->hwnd))) {
                    target_root = g_taskbarHwnd;
                }
                if (target_root) {
                    s_last_hook_pt = msg->pt;
                    PostMessageW(target_root, WM_TE_TASKBAR_MOUSEMOVE, (WPARAM)msg->hwnd, TE_PACK_POINT(msg->pt.x, msg->pt.y));
                }
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK TE_CBTHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    /* WH_CBT hook proc: kept for compatibility */
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

HINSTANCE TE_EngineGetInstance(void)
{
    return g_hinstDLL;
}

void TE_EngineResetInit(void)
{
    InterlockedExchange(&g_is_initialized, 2);
    g_taskbarHwnd = NULL;
}
