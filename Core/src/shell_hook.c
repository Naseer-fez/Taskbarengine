#include "core/shell_hook.h"

static TE_EventEntry* g_event_table = NULL;
static uint32_t* g_sub_count = NULL;
static UINT g_shell_msg = 0;

HRESULT TE_ShellHookStart(HWND hwnd, TE_EventEntry* event_table, uint32_t* sub_count)
{
    if (!hwnd || !event_table || !sub_count) return E_POINTER;
    g_shell_msg = RegisterWindowMessageW(L"SHELLHOOK");
    if (!g_shell_msg) return HRESULT_FROM_WIN32(GetLastError());
    if (!RegisterShellHookWindow(hwnd)) return HRESULT_FROM_WIN32(GetLastError());
    g_event_table = event_table;
    g_sub_count = sub_count;
    return S_OK;
}

void TE_ShellHookStop(HWND hwnd)
{
    if (hwnd) {
        DeregisterShellHookWindow(hwnd);
    }
    g_event_table = NULL;
    g_sub_count = NULL;
}

bool TE_ShellHookHandleMessage(UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (g_shell_msg == 0 || msg != g_shell_msg || !g_event_table || !g_sub_count) return false;

    TE_ShellHookEvent evt = {
        .code = (int)wparam,
        .wparam = wparam,
        .lparam = lparam
    };
    TE_EventDispatch(g_event_table, *g_sub_count, TE_EVENT_SHELL_HOOK, &evt);
    return true;
}
