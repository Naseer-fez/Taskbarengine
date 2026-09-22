#include "core/shell_hook.h"
#include "core/event_dispatch.h"
#include <sdk/te_events.h>

static UINT g_shell_hook_msg = 0;
static HWND g_shell_hook_hwnd = NULL;
static const wchar_t* SHELL_HOOK_CLASS = L"TE_ShellHookWindow";

static LRESULT CALLBACK ShellHookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_shell_hook_msg && g_shell_hook_msg != 0) {
        TE_ShellHookProcess(wParam, lParam);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HRESULT TE_ShellHookInit(HWND taskbar_hwnd) {
    (void)taskbar_hwnd;
    g_shell_hook_msg = RegisterWindowMessageW(L"SHELLHOOK");

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = ShellHookWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = SHELL_HOOK_CLASS;
    RegisterClassW(&wc);

    g_shell_hook_hwnd = CreateWindowExW(0, SHELL_HOOK_CLASS, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!g_shell_hook_hwnd) {
        return TE_E_FAIL;
    }

    if (!RegisterShellHookWindow(g_shell_hook_hwnd)) {
        DestroyWindow(g_shell_hook_hwnd);
        g_shell_hook_hwnd = NULL;
        UnregisterClassW(SHELL_HOOK_CLASS, wc.hInstance);
        return TE_E_FAIL;
    }
    return TE_S_OK;
}

void TE_ShellHookShutdown(HWND taskbar_hwnd) {
    (void)taskbar_hwnd;
    if (g_shell_hook_hwnd) {
        DeregisterShellHookWindow(g_shell_hook_hwnd);
        DestroyWindow(g_shell_hook_hwnd);
        g_shell_hook_hwnd = NULL;
        UnregisterClassW(SHELL_HOOK_CLASS, GetModuleHandleW(NULL));
    }
}

UINT TE_ShellHookGetMessageId(void) {
    return g_shell_hook_msg;
}

void TE_ShellHookProcess(WPARAM wParam, LPARAM lParam) {
    TE_ShellHookData data;
    data.shell_msg = (int)wParam;
    data.target_hwnd = (HWND)lParam;
    TE_EventDispatchFire(TE_EVENT_SHELL_HOOK, &data);
}
