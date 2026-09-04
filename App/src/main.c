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

static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
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

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hprev_instance, PWSTR cmd_line, int cmd_show)
{
    (void)hprev_instance;
    (void)cmd_line;
    (void)cmd_show;

    LogHost("wWinMain entered");

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

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = hinstance;
    wc.lpszClassName = L"TaskbarEngineHost";

    if (!RegisterClassW(&wc)) {
        LogHost("Failed to register window class");
        MessageBoxW(NULL, L"Failed to register window class.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
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
        return 1;
    }
    LogHost("Hidden window created");

    if (FAILED(TE_TrayCreate(g_main_hwnd, TE_TRAY_CALLBACK_MSG))) {
        LogHost("Failed to create tray icon");
        MessageBoxW(NULL, L"Failed to create tray icon.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
        DestroyWindow(g_main_hwnd);
        return 1;
    }
    LogHost("TE_TrayCreate succeeded");

    HWND taskbar_hwnd = NULL;
    for (int retry = 0; retry < 10; retry++) {
        taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
        if (taskbar_hwnd) break;
        Sleep(200);
    }

    if (!taskbar_hwnd) {
        LogHost("Failed to find Shell_TrayWnd");
        MessageBoxW(NULL, L"Failed to find Shell_TrayWnd.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
        PostMessageW(g_main_hwnd, WM_CLOSE, 0, 0);
    } else {
        LogHost("Found Shell_TrayWnd");
        DWORD explorer_thread_id = GetWindowThreadProcessId(taskbar_hwnd, NULL);

        WCHAR exe_path[MAX_PATH];
        if (GetModuleFileNameW(NULL, exe_path, ARRAYSIZE(exe_path)) == 0) {
            LogHost("Failed to get module file name");
            MessageBoxW(NULL, L"Failed to get module file name.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
            PostMessageW(g_main_hwnd, WM_CLOSE, 0, 0);
        } else {
            WCHAR* last_slash = wcsrchr(exe_path, L'\\');
            if (last_slash) {
                *(last_slash + 1) = L'\0';
                StringCchCatW(exe_path, ARRAYSIZE(exe_path), L"EngineDLL.dll");

                g_dll_handle = LoadLibraryW(exe_path);
                if (!g_dll_handle) {
                    LogHost("Failed to load EngineDLL.dll");
                    MessageBoxW(NULL, L"Failed to load EngineDLL.dll.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
                    PostMessageW(g_main_hwnd, WM_CLOSE, 0, 0);
                } else {
                    LogHost("EngineDLL.dll loaded");
                    HOOKPROC hook_proc = (HOOKPROC)GetProcAddress(g_dll_handle, "TE_GetMsgHookProc");
                    if (!hook_proc) {
                        hook_proc = (HOOKPROC)GetProcAddress(g_dll_handle, "TE_CBTHookProc");
                    }
                    if (!hook_proc) {
                        LogHost("Failed to find hook procedure");
                        MessageBoxW(NULL, L"Failed to find hook procedure in EngineDLL.dll.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
                        PostMessageW(g_main_hwnd, WM_CLOSE, 0, 0);
                    } else {
                        g_hook = SetWindowsHookExW(WH_GETMESSAGE, hook_proc, g_dll_handle, explorer_thread_id);
                        if (!g_hook) {
                            LogHost("Failed to install WH_GETMESSAGE hook");
                            MessageBoxW(NULL, L"Failed to install WH_GETMESSAGE hook.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
                            PostMessageW(g_main_hwnd, WM_CLOSE, 0, 0);
                        } else {
                            LogHost("Hook installed successfully");
                            TE_CrashRecoveryStart(g_main_hwnd, g_dll_handle);
                            PostMessageW(taskbar_hwnd, WM_NULL, 0, 0);
                        }
                    }
                }
            } else {
                LogHost("Failed to parse module file name");
                MessageBoxW(NULL, L"Failed to parse module file name.", L"TaskbarEngine Error", MB_OK | MB_ICONERROR);
                PostMessageW(g_main_hwnd, WM_CLOSE, 0, 0);
            }
        }
    }

    LogHost("Entering message loop");
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    LogHost("Exited message loop");

    return (int)msg.wParam;
}
