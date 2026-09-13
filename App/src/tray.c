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
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    g_nid.uCallbackMessage = callback_msg;

    /* Attempt to load custom app icon if present */
    WCHAR exe_dir[MAX_PATH];
    HICON hCustomIcon = NULL;
    if (GetModuleFileNameW(NULL, exe_dir, MAX_PATH)) {
        WCHAR* slash = wcsrchr(exe_dir, L'\\');
        if (slash) {
            *(slash + 1) = L'\0';
            WCHAR ico_path[MAX_PATH];
            StringCchPrintfW(ico_path, MAX_PATH, L"%sapp.ico", exe_dir);
            hCustomIcon = (HICON)LoadImageW(NULL, ico_path, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
        }
    }

    g_nid.hIcon = hCustomIcon ? hCustomIcon : LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"TaskbarEngine");
    StringCchCopyW(g_nid.szInfo, ARRAYSIZE(g_nid.szInfo), L"TaskbarEngine is active and running in the system tray.");
    StringCchCopyW(g_nid.szInfoTitle, ARRAYSIZE(g_nid.szInfoTitle), L"TaskbarEngine");
    g_nid.dwInfoFlags = NIIF_INFO;

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
