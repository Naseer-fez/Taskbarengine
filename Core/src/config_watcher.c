#include "core/config_watcher.h"
#include <windows.h>
#include <sdk/te_log.h>

static HANDLE g_thread_handle = NULL;
static HANDLE g_stop_event = NULL;
static HANDLE g_timer_handle = NULL;
static HWND g_notify_hwnd = NULL;
static wchar_t g_watch_dir[MAX_PATH] = { 0 };
static volatile LONG g_watcher_running = 0;

static HANDLE g_debounce_active_event = NULL;

static VOID CALLBACK DebounceTimerCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
    (void)lpParameter;
    (void)TimerOrWaitFired;
    InterlockedExchangePointer((PVOID volatile*)&g_timer_handle, NULL);
    HWND hwnd = g_notify_hwnd;
    if (g_watcher_running && hwnd && IsWindow(hwnd)) {
        TE_LogWrite(TE_LOG_INFO, "Config file change debounced (100ms), posting WM_TE_CONFIG_CHANGED");
        PostMessageW(hwnd, WM_TE_CONFIG_CHANGED, 0, 0);
    }
    if (g_debounce_active_event) {
        SetEvent(g_debounce_active_event);
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
    if (!ov.hEvent) {
        TE_LogWrite(TE_LOG_ERROR, "Failed to create config watcher event (error %lu)", GetLastError());
        CloseHandle(hdir);
        return 1;
    }

    HANDLE handles[2] = { g_stop_event, ov.hEvent };

    while (g_watcher_running) {
        DWORD bytes_returned = 0;
        ResetEvent(ov.hEvent);

        BOOL ok = ReadDirectoryChangesW(hdir, buffer, sizeof(buffer), FALSE,
                                        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME,
                                        &bytes_returned, &ov, NULL);
        if (!ok && GetLastError() != ERROR_IO_PENDING) {
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
            BOOL is_target = FALSE;
            DWORD offset = 0;
            PFILE_NOTIFY_INFORMATION pni = (PFILE_NOTIFY_INFORMATION)buffer;
            do {
                if (pni->FileNameLength > 0) {
                    wchar_t filename[MAX_PATH] = { 0 };
                    DWORD chars = pni->FileNameLength / sizeof(wchar_t);
                    if (chars < MAX_PATH) {
                        wcsncpy(filename, pni->FileName, chars);
                        filename[chars] = L'\0';
                        if (_wcsicmp(filename, L"config.jsonc") == 0) {
                            is_target = TRUE;
                            break;
                        }
                    }
                }
                offset = pni->NextEntryOffset;
                pni = (PFILE_NOTIFY_INFORMATION)((LPBYTE)pni + offset);
            } while (offset != 0);

            if (is_target) {
                HANDLE old_timer = (HANDLE)InterlockedExchangePointer((PVOID volatile*)&g_timer_handle, NULL);
                if (old_timer) {
                    DeleteTimerQueueTimer(NULL, old_timer, NULL);
                }
                if (g_debounce_active_event) {
                    ResetEvent(g_debounce_active_event);
                }
                HANDLE new_timer = NULL;
                if (CreateTimerQueueTimer(&new_timer, NULL, DebounceTimerCallback, NULL, 100, 0, WT_EXECUTEONLYONCE)) {
                    InterlockedExchangePointer((PVOID volatile*)&g_timer_handle, new_timer);
                } else if (g_debounce_active_event) {
                    SetEvent(g_debounce_active_event);
                }
            }
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
    g_watch_dir[MAX_PATH - 1] = L'\0';
    g_notify_hwnd = notify_hwnd;

    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_stop_event) return HRESULT_FROM_WIN32(GetLastError());
    
    g_debounce_active_event = CreateEventW(NULL, TRUE, TRUE, NULL); // Manual reset, initially signaled
    if (!g_debounce_active_event) {
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    
    g_watcher_running = 1;

    g_thread_handle = CreateThread(NULL, 0, WatcherThreadProc, NULL, 0, NULL);
    if (!g_thread_handle) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        g_watcher_running = 0;
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
        return hr;
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
        WaitForSingleObject(g_thread_handle, INFINITE);
        CloseHandle(g_thread_handle);
        g_thread_handle = NULL;
    }

    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
    }

    HANDLE old_timer = (HANDLE)InterlockedExchangePointer((PVOID volatile*)&g_timer_handle, NULL);
    if (old_timer) {
        DeleteTimerQueueTimer(NULL, old_timer, INVALID_HANDLE_VALUE);
    } else if (g_debounce_active_event) {
        WaitForSingleObject(g_debounce_active_event, INFINITE);
    }
    
    if (g_debounce_active_event) {
        CloseHandle(g_debounce_active_event);
        g_debounce_active_event = NULL;
    }
    
    g_notify_hwnd = NULL;
}
