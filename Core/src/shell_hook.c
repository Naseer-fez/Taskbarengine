#include "core/shell_hook.h"
#include "core/event_dispatch.h"
#include <sdk/te_events.h>

static UINT g_shell_hook_msg = 0;

HRESULT TE_ShellHookInit(HWND taskbar_hwnd) {
    g_shell_hook_msg = RegisterWindowMessageW(L"SHELLHOOK");
    if (!RegisterShellHookWindow(taskbar_hwnd)) {
        return TE_E_FAIL;
    }
    return TE_S_OK;
}

void TE_ShellHookShutdown(HWND taskbar_hwnd) {
    DeregisterShellHookWindow(taskbar_hwnd);
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
