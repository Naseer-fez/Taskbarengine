#include "core/config_watcher.h"
#include "core/core_manager.h"
#include <windows.h>

static HANDLE g_hDir = INVALID_HANDLE_VALUE;
static HANDLE g_stop_event = NULL;
static HANDLE g_thread = NULL;
static HWND g_taskbar_hwnd = NULL;

static DWORD WINAPI WatcherThread(LPVOID param) {
    (void)param;
    DWORD align_buf[1024 / sizeof(DWORD)];
    DWORD bytes = 0;
    
    OVERLAPPED ol = {0};
    ol.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ol.hEvent) return 1;
    
    HANDLE handles[2] = { g_stop_event, ol.hEvent };
    
    while (TRUE) {
        if (WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0) {
            break;
        }
        
        ResetEvent(ol.hEvent);
        if (!ReadDirectoryChangesW(g_hDir, align_buf, sizeof(align_buf), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, &ol, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) break;
        }
        
        DWORD wait_res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait_res == WAIT_OBJECT_0) {
            CancelIo(g_hDir);
            break;
        } else if (wait_res == WAIT_OBJECT_0 + 1) {
            // A change occurred, enter debounce loop
            BOOL settling = TRUE;
            while (settling) {
                ResetEvent(ol.hEvent);
                if (!ReadDirectoryChangesW(g_hDir, align_buf, sizeof(align_buf), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, &ol, NULL)) {
                    settling = FALSE;
                    break;
                }
                
                DWORD debounce_res = WaitForMultipleObjects(2, handles, FALSE, 200); // 200ms debounce
                if (debounce_res == WAIT_OBJECT_0) {
                    CancelIo(g_hDir);
                    settling = FALSE;
                    break;
                } else if (debounce_res == WAIT_TIMEOUT) {
                    CancelIo(g_hDir); // Stop the pending read, we're done debouncing
                    WaitForSingleObject(ol.hEvent, INFINITE); // Wait for cancellation to complete
                    PostMessage(g_taskbar_hwnd, WM_TE_IPC_COMMAND, TE_CMD_RELOAD_CONFIG, 0);
                    settling = FALSE;
                } else {
                    // Another change happened within 200ms, loop again to reset timer
                }
            }
        } else {
            break;
        }
    }
    
    CloseHandle(ol.hEvent);
    return 0;
}

HRESULT TE_ConfigWatcherStart(const wchar_t* config_dir, HWND taskbar_hwnd) {
    if (!config_dir || !taskbar_hwnd) return TE_E_INVALIDARG;
    
    g_taskbar_hwnd = taskbar_hwnd;
    
    g_hDir = CreateFileW(config_dir, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
                         
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
