#include "core/config_watcher.h"
#include "core/core_manager.h"
#include <sdk/te_log.h>
#include <windows.h>
#include <stdio.h>

static HANDLE g_hDir = INVALID_HANDLE_VALUE;
static HANDLE g_stop_event = NULL;
static HANDLE g_thread = NULL;
static HWND g_taskbar_hwnd = NULL;

static DWORD WINAPI WatcherThread(LPVOID param) {
    (void)param;
    DWORD align_buf[1024 / sizeof(DWORD)];
    
    OVERLAPPED ol = {0};
    ol.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ol.hEvent) return 1;
    
    HANDLE handles[2] = { g_stop_event, ol.hEvent };
    const DWORD notify_filter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE;

    TE_LogWrite(TE_LOG_INFO, "ConfigWatcher", "Watcher thread running");

    while (TRUE) {
        if (WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0) {
            break;
        }
        
        ResetEvent(ol.hEvent);
        if (!ReadDirectoryChangesW(g_hDir, align_buf, sizeof(align_buf), FALSE, notify_filter, NULL, &ol, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "ReadDirectoryChangesW failed: %lu", err);
                TE_LogWrite(TE_LOG_WARNING, "ConfigWatcher", err_buf);
                break;
            }
        }
        
        DWORD wait_res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait_res == WAIT_OBJECT_0) {
            CancelIo(g_hDir);
            break;
        } else if (wait_res == WAIT_OBJECT_0 + 1) {
            // Change detected - debounce for 150ms
            if (WaitForSingleObject(g_stop_event, 150) == WAIT_OBJECT_0) {
                CancelIo(g_hDir);
                break;
            }
            TE_LogWrite(TE_LOG_INFO, "ConfigWatcher", "Config directory change detected, posting TE_CMD_RELOAD_CONFIG");
            PostMessageW(g_taskbar_hwnd, WM_TE_IPC_COMMAND, TE_CMD_RELOAD_CONFIG, 0);
        } else {
            break;
        }
    }
    
    CloseHandle(ol.hEvent);
    TE_LogWrite(TE_LOG_INFO, "ConfigWatcher", "Watcher thread exiting");
    return 0;
}

HRESULT TE_ConfigWatcherStart(const wchar_t* config_dir, HWND taskbar_hwnd) {
    if (!config_dir || !taskbar_hwnd) return TE_E_INVALIDARG;
    
    g_taskbar_hwnd = taskbar_hwnd;
    
    g_hDir = CreateFileW(config_dir, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
                         
    if (g_hDir == INVALID_HANDLE_VALUE) {
        char err_buf[512];
        snprintf(err_buf, sizeof(err_buf), "Failed to open config dir '%ls', error=%lu", config_dir, GetLastError());
        TE_LogWrite(TE_LOG_ERROR, "ConfigWatcher", err_buf);
        return TE_E_FAIL;
    }
    
    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_stop_event) {
        CloseHandle(g_hDir);
        g_hDir = INVALID_HANDLE_VALUE;
        return TE_E_FAIL;
    }

    g_thread = CreateThread(NULL, 0, WatcherThread, NULL, 0, NULL);
    if (!g_thread) {
        CloseHandle(g_stop_event);
        CloseHandle(g_hDir);
        g_stop_event = NULL;
        g_hDir = INVALID_HANDLE_VALUE;
        return TE_E_FAIL;
    }
    
    char log_buf[512];
    snprintf(log_buf, sizeof(log_buf), "Config watcher started successfully on '%ls'", config_dir);
    TE_LogWrite(TE_LOG_INFO, "ConfigWatcher", log_buf);
    return TE_S_OK;
}

void TE_ConfigWatcherStop(void) {
    if (g_stop_event) {
        SetEvent(g_stop_event);
        if (g_hDir != INVALID_HANDLE_VALUE) {
            CancelIo(g_hDir);
        }
        WaitForSingleObject(g_thread, 3000);
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
