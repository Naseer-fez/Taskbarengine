#include "sdk/te_log_impl.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef _MSC_VER
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#endif

static TE_LogEntry g_ring_buffer[TE_LOG_RING_SIZE];
static volatile LONG g_write_idx = 0;
static volatile LONG g_read_idx = 0;

static HANDLE g_flush_thread = NULL;
static HANDLE g_flush_event = NULL;
static volatile LONG g_log_running = 0;

static wchar_t g_log_dir_path[MAX_PATH] = {0};
static wchar_t g_log_file_path[MAX_PATH] = {0};
static TE_LogLevel g_min_level = TE_LOG_INFO;
static bool g_log_to_file = false;

static void TE_RotateLogs(const wchar_t* dir)
{
    wchar_t search_pattern[MAX_PATH];
    swprintf(search_pattern, MAX_PATH, L"%s\\taskbarengine_*.log", dir);

    WIN32_FIND_DATAW find_data;
    HANDLE hfind = FindFirstFileW(search_pattern, &find_data);
    if (hfind == INVALID_HANDLE_VALUE) {
        return;
    }

    typedef struct FileInfo {
        wchar_t path[MAX_PATH];
        FILETIME ft;
    } FileInfo;

    FileInfo* files = (FileInfo*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FileInfo) * 32);
    if (!files) {
        FindClose(hfind);
        return;
    }
    int count = 0;

    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            swprintf(files[count].path, MAX_PATH, L"%s\\%s", dir, find_data.cFileName);
            files[count].ft = find_data.ftLastWriteTime;
            count++;
            if (count >= 32) break;
        }
    } while (FindNextFileW(hfind, &find_data));
    FindClose(hfind);

    /* Sort files by LastWriteTime ascending (oldest first) */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (CompareFileTime(&files[i].ft, &files[j].ft) > 0) {
                FileInfo tmp = files[i];
                files[i] = files[j];
                files[j] = tmp;
            }
        }
    }

    /* Delete oldest if count > 5 */
    while (count > 5) {
        DeleteFileW(files[0].path);
        for (int i = 0; i < count - 1; i++) {
            files[i] = files[i + 1];
        }
        count--;
    }

    HeapFree(GetProcessHeap(), 0, files);
}

static DWORD WINAPI TE_LogFlushThreadProc(LPVOID param)
{
    (void)param;
    while (g_log_running) {
        WaitForSingleObject(g_flush_event, 100);

        if (!g_log_to_file || g_log_file_path[0] == L'\0') {
            /* If file logging is disabled, drain and clear unread entries */
            LONG r = g_read_idx;
            LONG w = g_write_idx;
            while (r != w) {
                uint32_t idx = ((uint32_t)r) & (TE_LOG_RING_SIZE - 1);
                TE_LogEntry* entry = &g_ring_buffer[idx];
                if (InterlockedCompareExchange(&entry->state, TE_LOG_STATE_READING, TE_LOG_STATE_UNREAD) == TE_LOG_STATE_UNREAD) {
                    InterlockedExchange(&entry->state, TE_LOG_STATE_EMPTY);
                    r++;
                } else {
                    break;
                }
            }
            g_read_idx = r;
            continue;
        }

        LONG r = g_read_idx;
        LONG w = g_write_idx;

        if (r == w) continue;

        HANDLE hfile = CreateFileW(g_log_file_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hfile == INVALID_HANDLE_VALUE) {
            /* Skip draining if unable to open file */
            continue;
        }

        while (r != w) {
            uint32_t idx = ((uint32_t)r) & (TE_LOG_RING_SIZE - 1);
            TE_LogEntry* entry = &g_ring_buffer[idx];

            if (InterlockedCompareExchange(&entry->state, TE_LOG_STATE_READING, TE_LOG_STATE_UNREAD) != TE_LOG_STATE_UNREAD) {
                /* Slot is not ready for reading */
                break;
            }

            const char* level_str = "[INFO]";
            switch (entry->level) {
                case TE_LOG_DEBUG: level_str = "[DEBUG]"; break;
                case TE_LOG_INFO:  level_str = "[INFO]";  break;
                case TE_LOG_WARN:  level_str = "[WARN]";  break;
                case TE_LOG_ERROR: level_str = "[ERROR]"; break;
            }

            char formatted[512];
            int len = snprintf(formatted, sizeof(formatted), "%lu %s %s\r\n",
                               (unsigned long)entry->timestamp_ms, level_str, entry->message);
            if (len > 0) {
                DWORD written = 0;
                WriteFile(hfile, formatted, (DWORD)len, &written, NULL);
            }

            InterlockedExchange(&entry->state, TE_LOG_STATE_EMPTY);
            r++;
        }

        CloseHandle(hfile);
        g_read_idx = r;
    }
    return 0;
}

HRESULT TE_LogInit(const wchar_t* log_dir, TE_LogLevel min_level, bool to_file)
{
    if (g_log_running) {
        return S_OK;
    }

    g_min_level = min_level;
    g_log_to_file = to_file;

    if (log_dir != NULL && log_dir[0] != L'\0') {
        wcsncpy(g_log_dir_path, log_dir, MAX_PATH - 1);
    } else {
        PWSTR local_appdata = NULL;
        HRESULT hr = SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &local_appdata);
        if (SUCCEEDED(hr)) {
            swprintf(g_log_dir_path, MAX_PATH, L"%s\\TaskbarEngine\\logs", local_appdata);
            CoTaskMemFree(local_appdata);
        } else {
            wcscpy(g_log_dir_path, L"logs");
        }
    }

    if (!CreateDirectoryW(g_log_dir_path, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS && err != ERROR_ACCESS_DENIED) {
            /* Log directory creation warning in debug output */
#ifdef TE_DEBUG
            OutputDebugStringA("[TE_WARN] Failed to create log directory\n");
#endif
        }
    }

    if (g_log_to_file) {
        TE_RotateLogs(g_log_dir_path);

        SYSTEMTIME st;
        GetLocalTime(&st);
        swprintf(g_log_file_path, MAX_PATH, L"%s\\taskbarengine_%04d-%02d-%02d.log",
                 g_log_dir_path, st.wYear, st.wMonth, st.wDay);
    }

    g_write_idx = 0;
    g_read_idx = 0;
    ZeroMemory(g_ring_buffer, sizeof(g_ring_buffer));
    g_flush_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_log_running = 1;

    g_flush_thread = CreateThread(NULL, 0, TE_LogFlushThreadProc, NULL, 0, NULL);
    if (!g_flush_thread) {
        g_log_running = 0;
        CloseHandle(g_flush_event);
        g_flush_event = NULL;
        return E_FAIL;
    }

    return S_OK;
}

void TE_LogShutdown(void)
{
    if (!g_log_running) return;

    g_log_running = 0;
    if (g_flush_event) {
        SetEvent(g_flush_event);
    }

    if (g_flush_thread) {
        WaitForSingleObject(g_flush_thread, 1000);
        CloseHandle(g_flush_thread);
        g_flush_thread = NULL;
    }

    if (g_flush_event) {
        CloseHandle(g_flush_event);
        g_flush_event = NULL;
    }
}

void TE_LogWriteV(TE_LogLevel level, const char* fmt, va_list args)
{
    if (level < g_min_level) return;

    LONG w = InterlockedIncrement(&g_write_idx) - 1;
    uint32_t idx = ((uint32_t)w) & (TE_LOG_RING_SIZE - 1);
    TE_LogEntry* entry = &g_ring_buffer[idx];

    /* Atomically claim slot from EMPTY to WRITING. If not EMPTY, slot is busy/full, drop log */
    if (InterlockedCompareExchange(&entry->state, TE_LOG_STATE_WRITING, TE_LOG_STATE_EMPTY) != TE_LOG_STATE_EMPTY) {
        return;
    }

    entry->level = (uint32_t)level;
    entry->timestamp_ms = (uint32_t)GetTickCount64();

    vsnprintf(entry->message, sizeof(entry->message), fmt, args);
    entry->message[sizeof(entry->message) - 1] = '\0';

    InterlockedExchange(&entry->state, TE_LOG_STATE_UNREAD);

#ifdef TE_DEBUG
    const char* prefix = "[TE_INFO]";
    switch (level) {
        case TE_LOG_DEBUG: prefix = "[TE_DEBUG]"; break;
        case TE_LOG_INFO:  prefix = "[TE_INFO]";  break;
        case TE_LOG_WARN:  prefix = "[TE_WARN]";  break;
        case TE_LOG_ERROR: prefix = "[TE_ERROR]"; break;
    }
    char debug_buf[512];
    snprintf(debug_buf, sizeof(debug_buf), "%s %s\n", prefix, entry->message);
    OutputDebugStringA(debug_buf);
#endif

    if (g_flush_event) {
        SetEvent(g_flush_event);
    }
}
