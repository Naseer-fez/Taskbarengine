#include "app/tray_menu.h"
#include "app/ipc_client.h"
#include <winreg.h>
#include <strsafe.h>
#include <string.h>

#define IDM_ENABLE_RESIZE    101
#define IDM_DISABLE_RESIZE   102
#define IDM_TOGGLE_STARTUP   103
#define IDM_EXIT             104

typedef struct TrayIpcWork {
    TE_IpcMsgType type;
    char payload[64];
} TrayIpcWork;

static DWORD WINAPI TrayIpcWorker(LPVOID param)
{
    TrayIpcWork* work = (TrayIpcWork*)param;
    if (work) {
        uint32_t length = work->payload[0] ? (uint32_t)strlen(work->payload) + 1 : 0;
        TE_IpcClientSendCommand(work->type, length ? work->payload : NULL, length);
        HeapFree(GetProcessHeap(), 0, work);
    }
    return 0;
}

static void QueueTrayIpcCommand(TE_IpcMsgType type, const char* payload)
{
    TrayIpcWork* work = (TrayIpcWork*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*work));
    if (!work) return;
    work->type = type;
    if (payload && FAILED(StringCchCopyA(work->payload, ARRAYSIZE(work->payload), payload))) {
        HeapFree(GetProcessHeap(), 0, work);
        return;
    }
    HANDLE thread = CreateThread(NULL, 0, TrayIpcWorker, work, 0, NULL);
    if (thread) CloseHandle(thread);
    else HeapFree(GetProcessHeap(), 0, work);
}

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
        QueueTrayIpcCommand(TE_IPC_MSG_ENABLE_PLUGIN, "taskbar_resize");
    } else if (cmd == IDM_DISABLE_RESIZE) {
        QueueTrayIpcCommand(TE_IPC_MSG_DISABLE_PLUGIN, "taskbar_resize");
    } else if (cmd == IDM_TOGGLE_STARTUP) {
        TraySetStartupEnabled(!startup);
    } else if (cmd == IDM_EXIT) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}
