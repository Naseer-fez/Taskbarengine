#include "app/tray_menu.h"
#include "app/ipc_client.h"

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
            ShellExecuteW(NULL, L"open", L"TaskbarEngineSettings.exe", NULL, NULL, SW_SHOWNORMAL);
            return true;

        case TE_TRAY_MENU_ENABLE_ALL:
            MessageBoxW(hwnd, L"Enable/Disable All is managed in the Settings UI.", L"TaskbarEngine", MB_OK | MB_ICONINFORMATION);
            return true;

        case TE_TRAY_MENU_RELOAD_CONFIG:
            TE_IpcClientReloadConfig();
            return true;

        case TE_TRAY_MENU_EXIT:
            TE_IpcClientShutdownEngine();
            DestroyWindow(hwnd);
            return true;

        default:
            return false;
    }
}
