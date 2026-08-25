#include "core/config_watcher.h"
#include "core/core_manager.h"
#include <windows.h>

static HANDLE g_hDir = INVALID_HANDLE_VALUE;
static HANDLE g_stop_event = NULL;
static HANDLE g_thread = NULL;
static HWND g_taskbar_hwnd = NULL;

static DWORD WINAPI WatcherThread(LPVOID param) {
    (void)param;
    char buffer[1024];
    DWORD bytes = 0;
    
    while (TRUE) {
        if (WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0) {
            break;
        }
        
        if (ReadDirectoryChangesW(g_hDir, buffer, sizeof(buffer), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE, &bytes, NULL, NULL)) {
            if (WaitForSingleObject(g_stop_event, 100) == WAIT_TIMEOUT) {
                PostMessage(g_taskbar_hwnd, WM_TE_IPC_COMMAND, TE_CMD_RELOAD_CONFIG, 0);
            } else {
                break;
            }
        } else {
            break;
        }
    }
    return 0;
}

HRESULT TE_ConfigWatcherStart(const wchar_t* config_dir, HWND taskbar_hwnd) {
    if (!config_dir || !taskbar_hwnd) return TE_E_INVALIDARG;
    
    g_taskbar_hwnd = taskbar_hwnd;
    
    g_hDir = CreateFileW(config_dir, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
                         
    if (g_hDir == INVALID_HANDLE_VALUE) {
        return TE_E_FAIL;
    }
    
    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_thread = CreateThread(NULL, 0, WatcherThread, NULL, 0, NULL);
    
    return TE_S_OK;
}

void TE_ConfigWatcherStop(void) {
    if (g_stop_event) {
        SetEvent(g_stop_event);
        if (g_hDir != INVALID_HANDLE_VALUE) {
            CancelSynchronousIo(g_thread);
        }
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        CloseHandle(g_stop_event);
        g_thread = NULL;
        g_stop_event = NULL;
    }
    if (g_hDir != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hDir);
        g_hDir = INVALID_HANDLE_VALUE;
    }
}
