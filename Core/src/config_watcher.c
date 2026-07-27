#include "core/config_watcher.h"
#include <windows.h>
#include <sdk/te_log.h>

static HANDLE g_thread_handle = NULL;
static HANDLE g_stop_event = NULL;
static HANDLE g_timer_handle = NULL;
static HWND g_notify_hwnd = NULL;
static wchar_t g_watch_dir[MAX_PATH] = { 0 };
static volatile LONG g_watcher_running = 0;

static VOID CALLBACK DebounceTimerCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
    (void)lpParameter;
    (void)TimerOrWaitFired;
    if (g_notify_hwnd && IsWindow(g_notify_hwnd)) {
        TE_LogWrite(TE_LOG_INFO, "Config file change debounced (100ms), posting WM_TE_CONFIG_CHANGED");
        PostMessageW(g_notify_hwnd, WM_TE_CONFIG_CHANGED, 0, 0);
    }
}

static DWORD WINAPI WatcherThreadProc(LPVOID param)
{
    (void)param;

    HANDLE hdir = CreateFileW(g_watch_dir, FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (hdir == INVALID_HANDLE_VALUE) {
        TE_LogWrite(TE_LOG_ERROR, "Failed to open directory for watching: %ls (error %lu)", g_watch_dir, GetLastError());
        return 1;
    }

    BYTE buffer[1024];
    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

    HANDLE handles[2] = { g_stop_event, ov.hEvent };

    while (g_watcher_running) {
        DWORD bytes_returned = 0;
        ResetEvent(ov.hEvent);

        BOOL ok = ReadDirectoryChangesW(hdir, buffer, sizeof(buffer), FALSE,
                                        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME,
                                        &bytes_returned, &ov, NULL);
        if (!ok) {
            TE_LogWrite(TE_LOG_WARN, "ReadDirectoryChangesW failed with error %lu", GetLastError());
            break;
        }

        DWORD wait_res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait_res == WAIT_OBJECT_0) {
            /* Stop event signaled */
            CancelIoEx(hdir, &ov);
            break;
        } else if (wait_res == WAIT_OBJECT_0 + 1) {
            /* Directory change signaled */
            if (g_timer_handle) {
                DeleteTimerQueueTimer(NULL, g_timer_handle, NULL);
                g_timer_handle = NULL;
            }
            CreateTimerQueueTimer(&g_timer_handle, NULL, DebounceTimerCallback, NULL, 100, 0, WT_EXECUTEONLYONCE);
        } else {
            break;
        }
    }

    CloseHandle(ov.hEvent);
    CloseHandle(hdir);
    return 0;
}

HRESULT TE_ConfigWatcherStart(const wchar_t* config_dir, HWND notify_hwnd)
{
    if (!config_dir || !notify_hwnd) return E_POINTER;
    if (g_watcher_running) return S_OK;

    wcsncpy(g_watch_dir, config_dir, MAX_PATH - 1);
    g_notify_hwnd = notify_hwnd;

    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_watcher_running = 1;

    g_thread_handle = CreateThread(NULL, 0, WatcherThreadProc, NULL, 0, NULL);
    if (!g_thread_handle) {
        g_watcher_running = 0;
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
        return E_FAIL;
    }

    TE_LogWrite(TE_LOG_INFO, "Started config watcher on directory: %ls", g_watch_dir);
    return S_OK;
}

void TE_ConfigWatcherStop(void)
{
    if (!g_watcher_running) return;

    g_watcher_running = 0;
    if (g_stop_event) {
        SetEvent(g_stop_event);
    }

    if (g_thread_handle) {
        WaitForSingleObject(g_thread_handle, 1000);
        CloseHandle(g_thread_handle);
        g_thread_handle = NULL;
    }

    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
    }

    if (g_timer_handle) {
        DeleteTimerQueueTimer(NULL, g_timer_handle, NULL);
        g_timer_handle = NULL;
    }
}
