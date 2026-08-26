#include "app/tray_menu.h"
#include "app/ipc_client.h"

#define IDM_ENABLE_RESIZE  101
#define IDM_DISABLE_RESIZE 102
#define IDM_EXIT           103

void TE_TrayMenuShow(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_ENABLE_RESIZE, L"Enable Taskbar Resize");
    AppendMenuW(hMenu, MF_STRING, IDM_DISABLE_RESIZE, L"Disable Taskbar Resize");
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
    } else if (cmd == IDM_EXIT) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}
