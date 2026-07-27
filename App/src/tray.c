#include "app/tray.h"
#include <shellapi.h>
#include <stdbool.h>

static NOTIFYICONDATAW g_nid = { 0 };
static bool g_tray_created = false;

HRESULT TE_TrayCreate(HWND hwnd_parent, HICON icon)
{
    if (g_tray_created) {
        return S_OK;
    }

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd_parent;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = icon;
    wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"TaskbarEngine");

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    g_tray_created = true;
    return S_OK;
}

HRESULT TE_TrayDestroy(void)
{
    if (!g_tray_created) {
        return S_OK;
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_tray_created = false;
    return S_OK;
}
