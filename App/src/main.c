#include "app/tray.h"
#include "app/tray_menu.h"
#include "app/crash_recovery.h"
#include "app/ipc_client.h"
#include "scheduler.h"
#include <sdk/te_types.h>
#include <sdk/te_log.h>
#include <sdk/te_debug_trace.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <tlhelp32.h>
#include <windows.h>

typedef LRESULT (CALLBACK *HOOKPROC_FUNC)(int nCode, WPARAM wParam, LPARAM lParam);

static HHOOK g_hook = NULL;
static HMODULE g_engine_dll = NULL;
static UINT g_taskbar_created_msg = 0;

#define REHOOK_TIMER_ID 1

static DWORD FindExplorerPid(void)
{
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    DWORD pid = 0;
    if (taskbar) {
        GetWindowThreadProcessId(taskbar, &pid);
    }
    return pid;
}

static HRESULT InstallEngineHook(void* context)
{
    (void)context;

    if (!g_engine_dll) {
        wchar_t dll_path[MAX_PATH];
        DWORD len = GetModuleFileNameW(NULL, dll_path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        wchar_t* slash = wcsrchr(dll_path, L'\\');
        if (!slash) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
        }
        size_t remaining = MAX_PATH - (size_t)(slash + 1 - dll_path);
        int ret = swprintf(slash + 1, remaining, L"EngineDLL.dll");
        if (ret < 0 || (size_t)ret >= remaining) {
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        g_engine_dll = LoadLibraryExW(dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!g_engine_dll) {
            char dbg[256]; sprintf(dbg, "[TE-DBG] App: LoadLibraryExW failed err=%lu\n", GetLastError()); TE_DebugTrace(dbg);
            return HRESULT_FROM_WIN32(GetLastError());
        }
        TE_DebugTrace("[TE-DBG] App: EngineDLL.dll loaded successfully\n");
    }

    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }

    HOOKPROC_FUNC hook_proc = (HOOKPROC_FUNC)GetProcAddress(g_engine_dll, "TE_CbtHookProc");
    if (!hook_proc) {
        TE_DebugTrace("[TE-DBG] App: GetProcAddress for TE_CbtHookProc failed\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!taskbar) {
        /* Shell_TrayWnd not found yet.  Do NOT install a global hook (tid=0)
         * because that injects EngineDLL into every process on the desktop,
         * destabilizes XAML island startup, and causes Shell_TrayWnd to be
         * silently destroyed.  Return E_PENDING so the caller retries later. */
        TE_DebugTrace("[TE-DBG] App: Shell_TrayWnd not found, REFUSING to install global hook\n");
        return E_PENDING;
    }

    DWORD tid = GetWindowThreadProcessId(taskbar, NULL);
    if (tid == 0) {
        TE_DebugTrace("[TE-DBG] App: GetWindowThreadProcessId returned 0, REFUSING hook\n");
        return E_FAIL;
    }
    {
        char dbg[256]; sprintf(dbg, "[TE-DBG] App: InstallEngineHook taskbar=0x%p tid=%lu\n", (void*)taskbar, (unsigned long)tid); TE_DebugTrace(dbg);
    }

    g_hook = SetWindowsHookExW(WH_CBT, hook_proc, g_engine_dll, tid);
    if (g_hook != NULL) {
        /* Post a harmless WM_NULL message to the taskbar window to force
         * Explorer's message queue to process a CBT event (HCBT_QS).
         * This instantly triggers TE_CbtHookProc and injects EngineDLL.dll
         * without waiting for the user to interact with a window. */
        PostMessageW(taskbar, WM_NULL, 0, 0);
        SendNotifyMessageW(taskbar, WM_NULL, 0, 0);
    }
    {
        char dbg[256]; sprintf(dbg, "[TE-DBG] App: SetWindowsHookExW result hook=0x%p err=%lu\n", (void*)g_hook, GetLastError()); TE_DebugTrace(dbg);
    }
    return g_hook ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE:
            break;

        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                TE_TrayMenuShow(hwnd);
                return 0;
            }
            if (lParam == WM_LBUTTONDBLCLK) {
                TE_TrayMenuOpenSettings();
                return 0;
            }
            break;

        case WM_COMMAND:
            if (TE_TrayMenuHandleCommand(hwnd, wParam)) {
                return 0;
            }
            break;

        case WM_DESTROY:
            TE_CrashRecoveryStop();
            TE_IpcClientShutdownEngine();
            if (g_hook != NULL) {
                UnhookWindowsHookEx(g_hook);
                g_hook = NULL;
            }
            if (g_engine_dll != NULL) {
                FreeLibrary(g_engine_dll);
                g_engine_dll = NULL;
            }
            TE_TrayDestroy();
            PostQuitMessage(0);
            return 0;

        case WM_TIMER:
            if (wParam == REHOOK_TIMER_ID) {
                TE_DebugTrace("[TE-DBG] App: REHOOK_TIMER fired, reinstalling hook\n");
                KillTimer(hwnd, REHOOK_TIMER_ID);
                HRESULT hr = InstallEngineHook(NULL);
                if (hr == E_PENDING) {
                    TE_DebugTrace("[TE-DBG] App: Taskbar still pending, retrying in 500ms\n");
                    SetTimer(hwnd, REHOOK_TIMER_ID, 500, NULL);
                } else if (SUCCEEDED(hr)) {
                    DWORD pid = FindExplorerPid();
                    if (pid != 0) {
                        TE_CrashRecoveryStop();
                        TE_CrashRecoveryStart(hwnd, pid, InstallEngineHook, NULL);
                    }
                }
                return 0;
            }
            break;

        default:
            if (msg == g_taskbar_created_msg) {
                TE_DebugTrace("[TE-DBG] App: TaskbarCreated message received\n");
                /* Delay re-hooking by 500ms to let Explorer's XAML taskbar
                 * complete its startup layout before we subclass it. */
                SetTimer(hwnd, REHOOK_TIMER_ID, 500, NULL);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)nCmdShow;

    if (pCmdLine) {
        if (wcsstr(pCmdLine, L"--install")) {
            wchar_t exe_path[MAX_PATH];
            GetModuleFileNameW(NULL, exe_path, MAX_PATH);
            TE_SchedulerRegisterTask(exe_path);
        } else if (wcsstr(pCmdLine, L"--uninstall")) {
            TE_SchedulerRemoveTask();
            return 0;
        }
    }

    g_taskbar_created_msg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TaskbarEngine_HiddenWnd";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"TaskbarEngine_HiddenWnd", L"TaskbarEngine Tray Host", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (hwnd == NULL) {
        TE_DebugTrace("[TaskbarEngine Tray] FATAL: CreateWindowExW for hidden window returned NULL\n");
        return 1;
    }

    HRESULT hook_hr = InstallEngineHook(NULL);
    if (SUCCEEDED(hook_hr)) {
        TE_DebugTrace("[TaskbarEngine Tray] SetWindowsHookEx WH_CBT successfully installed\n");
    } else if (hook_hr == E_PENDING) {
        TE_DebugTrace("[TaskbarEngine Tray] Taskbar not ready, scheduling retry in 500ms\n");
        SetTimer(hwnd, REHOOK_TIMER_ID, 500, NULL);
    } else {
        TE_DebugTrace("[TaskbarEngine Tray] Failed to install EngineDLL WH_CBT hook\n");
    }

    HICON app_icon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    if (app_icon == NULL) {
        app_icon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }
    TE_TrayCreate(hwnd, app_icon);

    DWORD explorer_pid = FindExplorerPid();
    if (explorer_pid != 0) {
        TE_CrashRecoveryStart(hwnd, explorer_pid, InstallEngineHook, NULL);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

#ifndef _MSC_VER
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    (void)pCmdLine;
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}
#endif
