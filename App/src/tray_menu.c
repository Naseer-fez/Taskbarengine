#include "app/tray_menu.h"
#include "app/ipc_client.h"
#include <winreg.h>
#include <strsafe.h>

#define IDM_ENABLE_RESIZE    101
#define IDM_DISABLE_RESIZE   102
#define IDM_TOGGLE_STARTUP   103
#define IDM_EXIT             104

static BOOL IsTrayStartupEnabled(void)
{
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;

    DWORD type = 0;
    DWORD size = 0;
    BOOL found = (RegQueryValueExW(hKey, L"TaskbarEngine", NULL, &type, NULL, &size) == ERROR_SUCCESS
                  && type == REG_SZ);
    RegCloseKey(hKey);
    return found;
}

static void TraySetStartupEnabled(BOOL enable)
{
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    if (enable) {
        WCHAR exe_path[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, exe_path, MAX_PATH)) {
            WCHAR* slash = wcsrchr(exe_path, L'\\');
            if (slash) {
                *(slash + 1) = L'\0';
                WCHAR engine_path[MAX_PATH];
                StringCchPrintfW(engine_path, MAX_PATH, L"\"%sTaskbarEngine.exe\"", exe_path);
                RegSetValueExW(hKey, L"TaskbarEngine", 0, REG_SZ,
                               (const BYTE*)engine_path,
                               (DWORD)((wcslen(engine_path) + 1) * sizeof(WCHAR)));
            }
        }
    } else {
        RegDeleteValueW(hKey, L"TaskbarEngine");
    }
    RegCloseKey(hKey);
}

void TE_TrayMenuShow(HWND hwnd, POINT pt) {
    BOOL startup = IsTrayStartupEnabled();

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_ENABLE_RESIZE, L"Enable Taskbar Resize");
    AppendMenuW(hMenu, MF_STRING, IDM_DISABLE_RESIZE, L"Disable Taskbar Resize");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | (startup ? MF_CHECKED : MF_UNCHECKED),
                IDM_TOGGLE_STARTUP, L"Run at Startup");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_ENABLE_RESIZE) {
        const char* name = "taskbar_resize";
        TE_IpcClientSendCommand(TE_IPC_MSG_ENABLE_PLUGIN, name, (uint32_t)strlen(name) + 1);
    } else if (cmd == IDM_DISABLE_RESIZE) {
        const char* name = "taskbar_resize";
        TE_IpcClientSendCommand(TE_IPC_MSG_DISABLE_PLUGIN, name, (uint32_t)strlen(name) + 1);
    } else if (cmd == IDM_TOGGLE_STARTUP) {
        TraySetStartupEnabled(!startup);
    } else if (cmd == IDM_EXIT) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}
