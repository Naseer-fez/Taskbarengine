#include "app/tray_menu.h"
#include "app/ipc_client.h"
#include <shellapi.h>
#include <wchar.h>

bool TE_TrayMenuOpenSettings(void)
{
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;

    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash || (size_t)(slash - path) + 1 + wcslen(L"TaskbarEngineSettings.exe") >= MAX_PATH) {
        return false;
    }
    wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), L"TaskbarEngineSettings.exe");
    return (INT_PTR)ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL) > 32;
}

void TE_TrayMenuShow(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, TE_TRAY_MENU_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_STRING, TE_TRAY_MENU_ENABLE_ALL, L"Enable/Disable All");
    AppendMenuW(menu, MF_STRING, TE_TRAY_MENU_RELOAD_CONFIG, L"Reload Config");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, TE_TRAY_MENU_ABOUT, L"About");
    AppendMenuW(menu, MF_STRING, TE_TRAY_MENU_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

bool TE_TrayMenuHandleCommand(HWND hwnd, WPARAM wparam)
{
    switch (LOWORD(wparam)) {
        case TE_TRAY_MENU_SETTINGS:
        case TE_TRAY_MENU_ABOUT:
            TE_TrayMenuOpenSettings();
            return true;

        case TE_TRAY_MENU_ENABLE_ALL:
            MessageBoxW(hwnd, L"Enable/Disable All is managed in the Settings UI.", L"TaskbarEngine", MB_OK | MB_ICONINFORMATION);
            return true;

        case TE_TRAY_MENU_RELOAD_CONFIG:
            TE_IpcClientReloadConfig();
            return true;

        case TE_TRAY_MENU_EXIT:
            HRESULT shutdown_hr = TE_IpcClientShutdownEngine();
            if (SUCCEEDED(shutdown_hr) || (FAILED(shutdown_hr) && HRESULT_CODE(shutdown_hr) == ERROR_FILE_NOT_FOUND)) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd,
                            L"The engine did not acknowledge shutdown. The tray app will remain running to avoid leaving Explorer modified.",
                            L"TaskbarEngine", MB_OK | MB_ICONERROR);
            }
            return true;

        default:
            return false;
    }
}
