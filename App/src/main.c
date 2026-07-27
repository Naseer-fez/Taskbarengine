#include "app/tray.h"
#include <sdk/te_types.h>
#include <sdk/te_log.h>
#include <windows.h>

typedef LRESULT (CALLBACK *HOOKPROC_FUNC)(int nCode, WPARAM wParam, LPARAM lParam);

static HHOOK g_hook = NULL;
static HMODULE g_engine_dll = NULL;

static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE:
            break;

        case WM_TRAYICON:
            break;

        case WM_DESTROY:
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
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)pCmdLine;
    (void)nCmdShow;

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TaskbarEngine_HiddenWnd";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"TaskbarEngine_HiddenWnd", L"TaskbarEngine Tray Host", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    if (hwnd == NULL) {
        return 1;
    }

    g_engine_dll = LoadLibraryW(L"EngineDLL.dll");
    if (g_engine_dll == NULL) {
        OutputDebugStringA("[TaskbarEngine Tray] Failed to load EngineDLL.dll\n");
    } else {
        HOOKPROC_FUNC hook_proc = (HOOKPROC_FUNC)GetProcAddress(g_engine_dll, "TE_CbtHookProc");
        if (hook_proc != NULL) {
            g_hook = SetWindowsHookExW(WH_CBT, hook_proc, g_engine_dll, 0);
            if (g_hook == NULL) {
                OutputDebugStringA("[TaskbarEngine Tray] SetWindowsHookEx failed\n");
            } else {
                OutputDebugStringA("[TaskbarEngine Tray] SetWindowsHookEx WH_CBT successfully installed\n");
            }
        } else {
            OutputDebugStringA("[TaskbarEngine Tray] Failed to find TE_CbtHookProc in EngineDLL.dll\n");
        }
    }

    HICON app_icon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    if (app_icon == NULL) {
        app_icon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }
    TE_TrayCreate(hwnd, app_icon);

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
