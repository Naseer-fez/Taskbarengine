#include "core/shell_hook.h"

static TE_EventEntry* g_event_table = NULL;
static uint32_t* g_sub_count = NULL;
static UINT g_shell_msg = 0;
static HWND g_hook_hwnd = NULL;

static LRESULT CALLBACK ShellHookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_shell_msg && g_event_table && g_sub_count) {
        TE_ShellHookEvent evt = {
            .code = (int)wParam,
            .wparam = wParam,
            .lparam = lParam
        };
        TE_EventDispatch(g_event_table, *g_sub_count, TE_EVENT_SHELL_HOOK, &evt);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HRESULT TE_ShellHookStart(HWND hwnd, TE_EventEntry* event_table, uint32_t* sub_count)
{
    if (!hwnd || !event_table || !sub_count) return E_POINTER;
    g_shell_msg = RegisterWindowMessageW(L"SHELLHOOK");
    if (!g_shell_msg) return HRESULT_FROM_WIN32(GetLastError());

    /* Create a separate hidden window for shell hook registration.
     * Registering Shell_TrayWnd itself conflicts with Explorer's own
     * internal shell hook handling and can corrupt its task list state. */
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = ShellHookWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"TE_ShellHookHelper";
    RegisterClassW(&wc);

    g_hook_hwnd = CreateWindowExW(0, L"TE_ShellHookHelper", L"", 0,
                                  0, 0, 0, 0, NULL, NULL,
                                  GetModuleHandleW(NULL), NULL);
    if (!g_hook_hwnd) return HRESULT_FROM_WIN32(GetLastError());

    if (!RegisterShellHookWindow(g_hook_hwnd)) {
        DestroyWindow(g_hook_hwnd);
        g_hook_hwnd = NULL;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    g_event_table = event_table;
    g_sub_count = sub_count;
    return S_OK;
}

void TE_ShellHookStop(HWND hwnd)
{
    (void)hwnd; /* No longer used — we use our own helper window */
    if (g_hook_hwnd) {
        DeregisterShellHookWindow(g_hook_hwnd);
        DestroyWindow(g_hook_hwnd);
        g_hook_hwnd = NULL;
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
