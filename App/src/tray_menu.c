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
            MessageBoxW(hwnd, L"Settings UI is planned for Phase 5.", L"TaskbarEngine", MB_OK | MB_ICONINFORMATION);
            return true;

        case TE_TRAY_MENU_ENABLE_ALL:
            MessageBoxW(hwnd, L"Enable/Disable All will be backed by config writes in the GUI phase.", L"TaskbarEngine", MB_OK | MB_ICONINFORMATION);
            return true;

        case TE_TRAY_MENU_RELOAD_CONFIG:
            TE_IpcClientReloadConfig();
            return true;

        case TE_TRAY_MENU_ABOUT:
            MessageBoxW(hwnd, L"TaskbarEngine", L"About", MB_OK | MB_ICONINFORMATION);
            return true;

        case TE_TRAY_MENU_EXIT:
            TE_IpcClientShutdownEngine();
            DestroyWindow(hwnd);
            return true;

        default:
            return false;
    }
}
