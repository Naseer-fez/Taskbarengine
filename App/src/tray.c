#include "app/tray.h"

#include <shellapi.h>
#include <strsafe.h>

#include <sdk/te_types.h>

static NOTIFYICONDATAW g_nid = {0};
static BOOL g_tray_created = FALSE;

HRESULT TE_TrayCreate(HWND hwnd_owner, UINT callback_msg)
{
    if (g_tray_created) {
        return TE_S_OK;
    }

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd_owner;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = callback_msg;
    /* Use MAKEINTRESOURCEW for wide-char LoadIconW compatibility */
    g_nid.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"TaskbarEngine");

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        return TE_E_FAIL;
    }

    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);

    g_tray_created = TRUE;
    return TE_S_OK;
}

void TE_TrayDestroy(void)
{
    if (g_tray_created) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_tray_created = FALSE;
    }
}
