#include "app/tray.h"
#include "app/tray_menu.h"
#include "app/crash_recovery.h"
#include "app/ipc_client.h"
#include <windows.h>
#include <strsafe.h>

#define TE_TRAY_CALLBACK_MSG (WM_APP + 1)

static HHOOK g_hook = NULL;
static HMODULE g_dll_handle = NULL;
static HWND g_main_hwnd = NULL;

#include <stdio.h>

static void LogHost(const char* msg)
{
    WCHAR path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
        WCHAR* last = wcsrchr(path, L'\\');
        if (last) {
            *(last + 1) = L'\0';
            StringCchCatW(path, MAX_PATH, L"te_host.log");
            FILE* f = _wfopen(path, L"a");
            if (f) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
                fclose(f);
            }
        }
    }
}

static UINT g_msg_taskbar_created = 0;
static BOOL InjectHook(void);

static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == g_msg_taskbar_created && g_msg_taskbar_created != 0) {
        LogHost("Received TaskbarCreated; restoring tray icon and hook");
        TE_TrayCreate(hwnd, TE_TRAY_CALLBACK_MSG);
        InjectHook();
        return 0;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "MainWindowProc msg: 0x%04X", msg);
    LogHost(buf);

    switch (msg) {
        case TE_TRAY_CALLBACK_MSG:
            LogHost("Received TE_TRAY_CALLBACK_MSG");
            switch (LOWORD(lparam)) {
                case WM_RBUTTONUP: {
                    POINT pt;
                    GetCursorPos(&pt);
                    TE_TrayMenuShow(hwnd, pt);
                    break;
                }
                case WM_LBUTTONDBLCLK:
                    MessageBoxW(NULL, L"TaskbarEngine", L"TaskbarEngine", MB_OK | MB_ICONINFORMATION);
                    break;
            }
            return 0;

        case WM_CLOSE:
            LogHost("Received WM_CLOSE");
            TE_IpcClientSendCommand(TE_IPC_MSG_SHUTDOWN, NULL, 0);
            Sleep(200);
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            LogHost("Received WM_DESTROY");
            TE_CrashRecoveryStop();
            if (g_hook) {
                UnhookWindowsHookEx(g_hook);
                g_hook = NULL;
            }
            TE_TrayDestroy();
            if (g_dll_handle) {
                FreeLibrary(g_dll_handle);
                g_dll_handle = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static BOOL InjectHook(void)
{
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }

    HWND taskbar_hwnd = NULL;
    for (int retry = 0; retry < 10; retry++) {
        taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
        if (taskbar_hwnd) break;
        Sleep(200);
    }

    if (!taskbar_hwnd) {
        LogHost("Failed to find Shell_TrayWnd");
        return FALSE;
    }

    LogHost("Found Shell_TrayWnd");
    DWORD explorer_thread_id = GetWindowThreadProcessId(taskbar_hwnd, NULL);
    if (explorer_thread_id == 0) {
        LogHost("Invalid thread ID for Shell_TrayWnd");
        return FALSE;
    }

    if (!g_dll_handle) {
        WCHAR exe_path[MAX_PATH];
        if (GetModuleFileNameW(NULL, exe_path, ARRAYSIZE(exe_path)) == 0) {
            LogHost("Failed to get module file name");
            return FALSE;
        }
        WCHAR* last_slash = wcsrchr(exe_path, L'\\');
        if (last_slash) {
            *(last_slash + 1) = L'\0';
            StringCchCatW(exe_path, ARRAYSIZE(exe_path), L"EngineDLL.dll");

            g_dll_handle = LoadLibraryW(exe_path);
            if (!g_dll_handle) {
                WCHAR alt_path[MAX_PATH];
                StringCchCopyW(alt_path, ARRAYSIZE(alt_path), exe_path);
                WCHAR* sep = wcsrchr(alt_path, L'\\');
                if (sep) {
                    *sep = L'\0';
                    StringCchCatW(alt_path, ARRAYSIZE(alt_path), L"\\Core\\EngineDLL.dll");
                    g_dll_handle = LoadLibraryW(alt_path);
                    if (!g_dll_handle) {
                        StringCchCopyW(alt_path, ARRAYSIZE(alt_path), exe_path);
                        sep = wcsrchr(alt_path, L'\\');
                        if (sep) {
                            *sep = L'\0';
                            StringCchCatW(alt_path, ARRAYSIZE(alt_path), L"\\..\\Core\\EngineDLL.dll");
                            g_dll_handle = LoadLibraryW(alt_path);
                        }
                    }
                }
            }
        }
    }

    if (!g_dll_handle) {
        LogHost("Failed to load EngineDLL.dll");
        return FALSE;
    }

    LogHost("EngineDLL.dll loaded");
    HOOKPROC hook_proc = (HOOKPROC)GetProcAddress(g_dll_handle, "TE_GetMsgHookProc");
    if (!hook_proc) hook_proc = (HOOKPROC)GetProcAddress(g_dll_handle, "_TE_GetMsgHookProc@12");
    if (!hook_proc) hook_proc = (HOOKPROC)GetProcAddress(g_dll_handle, "TE_CBTHookProc");
    if (!hook_proc) hook_proc = (HOOKPROC)GetProcAddress(g_dll_handle, "_TE_CBTHookProc@12");

    if (!hook_proc) {
        LogHost("Failed to find hook procedure");
        return FALSE;
    }

    g_hook = SetWindowsHookExW(WH_GETMESSAGE, hook_proc, g_dll_handle, explorer_thread_id);
    if (!g_hook) {
        LogHost("Failed to install WH_GETMESSAGE hook");
        return FALSE;
    }

    LogHost("Hook installed successfully");
    TE_CrashRecoveryStart(g_main_hwnd, g_dll_handle);
    PostMessageW(taskbar_hwnd, WM_NULL, 0, 0);
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hprev_instance, PWSTR cmd_line, int cmd_show)
{
    (void)hprev_instance;
    (void)cmd_line;
    (void)cmd_show;

    if (cmd_line && (wcsstr(cmd_line, L"-exit") || wcsstr(cmd_line, L"--exit") || wcsstr(cmd_line, L"/exit"))) {
        HANDLE hExit = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"TaskbarEngine_Exit_Event");
        if (hExit) {
            SetEvent(hExit);
            CloseHandle(hExit);
            LogHost("Signaled running instance to exit via command line");
        }
        return 0;
    }

    LogHost("wWinMain entered");

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"TaskbarEngine_Host_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        LogHost("Another instance is already running; exiting");
        return 0;
    }

    HANDLE hExitEvent = CreateEventW(NULL, FALSE, FALSE, L"TaskbarEngine_Exit_Event");

    /* Attach to the interactive user desktop if started from background */
    HDESK hDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hDesk) {
        hDesk = OpenDesktopW(L"Default", 0, FALSE, GENERIC_ALL);
    }
    if (hDesk) {
        SetThreadDesktop(hDesk);
        LogHost("SetThreadDesktop succeeded");
    } else {
        LogHost("Failed to open interactive desktop");
    }

    g_msg_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilter(g_msg_taskbar_created, MSGFLT_ADD);
    ChangeWindowMessageFilter(TE_TRAY_CALLBACK_MSG, MSGFLT_ADD);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = hinstance;
    wc.lpszClassName = L"TaskbarEngineHost";

    if (!RegisterClassW(&wc)) {
        LogHost("Failed to register window class");
        MessageBoxW(NULL, L"Failed to register window class.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
        if (hExitEvent) CloseHandle(hExitEvent);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    g_main_hwnd = CreateWindowExW(
        0,
        L"TaskbarEngineHost",
        L"TaskbarEngine",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        NULL,
        NULL,
        hinstance,
        NULL
    );

    if (!g_main_hwnd) {
        LogHost("Failed to create hidden window");
        MessageBoxW(NULL, L"Failed to create hidden window.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
        if (hExitEvent) CloseHandle(hExitEvent);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }
    LogHost("Hidden window created");

    ChangeWindowMessageFilterEx(g_main_hwnd, g_msg_taskbar_created, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(g_main_hwnd, TE_TRAY_CALLBACK_MSG, MSGFLT_ALLOW, NULL);

    if (FAILED(TE_TrayCreate(g_main_hwnd, TE_TRAY_CALLBACK_MSG))) {
        LogHost("Failed to create tray icon (continuing)");
    } else {
        LogHost("TE_TrayCreate succeeded");
    }

    if (!InjectHook()) {
        LogHost("Failed to inject hook into Explorer");
        MessageBoxW(NULL, L"Failed to inject hook into Explorer.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
        PostMessageW(g_main_hwnd, WM_CLOSE, 0, 0);
    }

    LogHost("Entering message loop");
    MSG msg = {0};
    BOOL running = TRUE;
    while (running) {
        DWORD wait_res = MsgWaitForMultipleObjectsEx(1, &hExitEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait_res == WAIT_OBJECT_0) {
            LogHost("Exit event signaled; closing");
            PostMessageW(g_main_hwnd, WM_CLOSE, 0, 0);
        }
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = FALSE;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    LogHost("Exited message loop");

    if (hExitEvent) {
        CloseHandle(hExitEvent);
    }
    if (hMutex) {
        CloseHandle(hMutex);
    }

    return (int)msg.wParam;
}
