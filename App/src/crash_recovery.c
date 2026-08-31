#include "app/crash_recovery.h"
#include <process.h>
#include <strsafe.h>

static HANDLE g_monitor_thread = NULL;
static BOOL g_stop_monitor = FALSE;
static HWND g_main_hwnd = NULL;
static HMODULE g_dll_handle = NULL;

static unsigned int __stdcall MonitorThread(void* arg) {
    (void)arg;
    while (!g_stop_monitor) {
        HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
        if (taskbar_hwnd) {
            DWORD explorer_pid;
            DWORD explorer_tid = GetWindowThreadProcessId(taskbar_hwnd, &explorer_pid);
            HANDLE hThread = OpenThread(SYNCHRONIZE, FALSE, explorer_tid);
            if (hThread) {
                while (!g_stop_monitor) {
                    DWORD res = WaitForSingleObject(hThread, 500);
                    if (res == WAIT_OBJECT_0) {
                        break;
                    }
                }
                CloseHandle(hThread);
                
                if (!g_stop_monitor) {
                    Sleep(2000);
                    
                    HWND new_taskbar = NULL;
                    for (int i=0; i<10; i++) {
                        new_taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
                        if (new_taskbar) break;
                        Sleep(500);
                    }
                    
                    if (new_taskbar) {
                        DWORD new_tid = GetWindowThreadProcessId(new_taskbar, NULL);
                        HOOKPROC hook_proc = (HOOKPROC)GetProcAddress(g_dll_handle, "TE_CBTHookProc");
                        if (hook_proc) {
                            HHOOK hook = SetWindowsHookExW(WH_CBT, hook_proc, g_dll_handle, new_tid);
                            if (hook) {
                                PostMessageW(new_taskbar, WM_NULL, 0, 0);
                                Sleep(50);
                                UnhookWindowsHookEx(hook);
                            }
                        }
                    }
                }
            }
        }
        Sleep(500);
    }
    return 0;
}

void TE_CrashRecoveryStart(HWND main_hwnd, HMODULE dll_handle) {
    g_main_hwnd = main_hwnd;
    g_dll_handle = dll_handle;
    g_stop_monitor = FALSE;
    g_monitor_thread = (HANDLE)_beginthreadex(NULL, 0, MonitorThread, NULL, 0, NULL);
}

void TE_CrashRecoveryStop(void) {
    if (g_monitor_thread) {
        g_stop_monitor = TRUE;
        WaitForSingleObject(g_monitor_thread, INFINITE);
        CloseHandle(g_monitor_thread);
        g_monitor_thread = NULL;
    }
}
