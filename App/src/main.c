#include "app/tray.h"
#include "app/tray_menu.h"
#include "app/crash_recovery.h"
#include <sdk/te_types.h>
#include <sdk/te_log.h>
#include <stdio.h>
#include <wchar.h>
#include <tlhelp32.h>
#include <windows.h>

typedef LRESULT (CALLBACK *HOOKPROC_FUNC)(int nCode, WPARAM wParam, LPARAM lParam);

static HHOOK g_hook = NULL;
static HMODULE g_engine_dll = NULL;
static HINSTANCE g_hinstance = NULL;
static UINT g_taskbar_created_msg = 0;

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
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }

    HOOKPROC_FUNC hook_proc = (HOOKPROC_FUNC)GetProcAddress(g_engine_dll, "TE_CbtHookProc");
    if (!hook_proc) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    g_hook = SetWindowsHookExW(WH_CBT, hook_proc, g_engine_dll, 0);
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
                MessageBoxW(hwnd, L"Settings UI is planned for Phase 5.", L"TaskbarEngine", MB_OK | MB_ICONINFORMATION);
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

        default:
            if (msg == g_taskbar_created_msg) {
                InstallEngineHook(NULL);
                DWORD pid = FindExplorerPid();
                if (pid != 0) {
                    TE_CrashRecoveryStop();
                    TE_CrashRecoveryStart(hwnd, pid, InstallEngineHook, NULL);
                }
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)pCmdLine;
    (void)nCmdShow;
    g_hinstance = hInstance;
    (void)g_hinstance;
    g_taskbar_created_msg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TaskbarEngine_HiddenWnd";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"TaskbarEngine_HiddenWnd", L"TaskbarEngine Tray Host", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    if (hwnd == NULL) {
        return 1;
    }

    if (FAILED(InstallEngineHook(NULL))) {
        OutputDebugStringA("[TaskbarEngine Tray] Failed to install EngineDLL WH_CBT hook\n");
    } else {
        OutputDebugStringA("[TaskbarEngine Tray] SetWindowsHookEx WH_CBT successfully installed\n");
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
