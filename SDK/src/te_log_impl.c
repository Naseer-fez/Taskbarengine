#include <sdk/te_log_impl.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define MAX_LOG_FILES 5
#define MAX_FILE_SIZE (5 * 1024 * 1024)
#define RING_BUFFER_SIZE 256
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)

typedef struct LogEntry {
    TE_LogLevel level;
    SYSTEMTIME timestamp;
    char module[32];
    char message[204];
} LogEntry;

static LogEntry g_ring_buffer[RING_BUFFER_SIZE];
static volatile LONG g_write_pos = 0;
static volatile LONG g_read_pos = 0;

static HANDLE g_flush_event = NULL;
static HANDLE g_shutdown_event = NULL;
static HANDLE g_flush_thread = NULL;

static TE_LogLevel g_min_level = TE_LOG_DEBUG;
static TE_LogFunc g_log_callback = NULL;

static wchar_t g_log_dir[MAX_PATH] = {0};
static FILE* g_log_file = NULL;

static const char* GetLevelString(TE_LogLevel level) {
    switch (level) {
        case TE_LOG_DEBUG:   return "DEBUG";
        case TE_LOG_INFO:    return "INFO ";
        case TE_LOG_WARNING: return "WARN ";
        case TE_LOG_ERROR:   return "ERROR";
        default:             return "UNKNN";
    }
}

static void RotateLogs(void) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }

    wchar_t old_path[MAX_PATH];
    wchar_t new_path[MAX_PATH];

    for (int i = MAX_LOG_FILES - 2; i >= 0; --i) {
        if (i == 0) {
            _snwprintf_s(old_path, MAX_PATH, _TRUNCATE, L"%s\\taskbarengine.log", g_log_dir);
        } else {
            _snwprintf_s(old_path, MAX_PATH, _TRUNCATE, L"%s\\taskbarengine.%d.log", g_log_dir, i);
        }
        _snwprintf_s(new_path, MAX_PATH, _TRUNCATE, L"%s\\taskbarengine.%d.log", g_log_dir, i + 1);

        MoveFileExW(old_path, new_path, MOVEFILE_REPLACE_EXISTING);
    }

    _snwprintf_s(old_path, MAX_PATH, _TRUNCATE, L"%s\\taskbarengine.log", g_log_dir);
    _wfopen_s(&g_log_file, old_path, L"a");
}

static void CheckRotation(void) {
    if (g_log_file) {
        long size = ftell(g_log_file);
        if (size >= MAX_FILE_SIZE) {
            RotateLogs();
        }
    }
}

static void WriteEntryToFile(const LogEntry* entry) {
    if (!g_log_file) {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\taskbarengine.log", g_log_dir);
        _wfopen_s(&g_log_file, path, L"a");
        if (!g_log_file) return;
    }

    fprintf(g_log_file, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] [%s] %s\n",
            entry->timestamp.wYear, entry->timestamp.wMonth, entry->timestamp.wDay,
            entry->timestamp.wHour, entry->timestamp.wMinute, entry->timestamp.wSecond,
            entry->timestamp.wMilliseconds,
            GetLevelString(entry->level),
            entry->module,
            entry->message);
    
    CheckRotation();
}

static void FlushPendingEntries(void) {
    LONG current_read = g_read_pos;
    LONG current_write = g_write_pos;
    
    // If the buffer has wrapped around and overwritten read_pos, fast-forward read_pos
    if (current_write - current_read > RING_BUFFER_SIZE) {
        current_read = current_write - RING_BUFFER_SIZE;
        g_read_pos = current_read; // Approximate update
    }

    while (current_read < current_write) {
        LONG index = current_read & RING_BUFFER_MASK;
        WriteEntryToFile(&g_ring_buffer[index]);
        current_read++;
        g_read_pos = current_read;
    }

    if (g_log_file) {
        fflush(g_log_file);
    }
}

static DWORD WINAPI FlushThreadProc(LPVOID lpParam) {
    (void)lpParam;
    HANDLE handles[2] = { g_shutdown_event, g_flush_event };

    while (TRUE) {
        DWORD wait_result = WaitForMultipleObjects(2, handles, FALSE, 100);
        if (wait_result == WAIT_OBJECT_0) {
            // Shutdown signaled
            break;
        } else if (wait_result == WAIT_OBJECT_0 + 1) {
            // Flush signaled
            ResetEvent(g_flush_event);
        }
        FlushPendingEntries();
    }

    // Final flush before exiting
    FlushPendingEntries();
    return 0;
}

HRESULT TE_LogInit(const wchar_t* log_dir, TE_LogLevel min_level) {
    if (!log_dir) return TE_E_INVALIDARG;

    wcsncpy_s(g_log_dir, MAX_PATH, log_dir, _TRUNCATE);
    g_min_level = min_level;

    g_flush_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_shutdown_event = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (!g_flush_event || !g_shutdown_event) {
        if (g_flush_event) CloseHandle(g_flush_event);
        if (g_shutdown_event) CloseHandle(g_shutdown_event);
        return TE_E_FAIL;
    }

    g_flush_thread = CreateThread(NULL, 0, FlushThreadProc, NULL, 0, NULL);
    if (!g_flush_thread) {
        CloseHandle(g_flush_event);
        CloseHandle(g_shutdown_event);
        return TE_E_FAIL;
    }

    return TE_S_OK;
}

void TE_LogShutdown(void) {
    if (g_shutdown_event) {
        SetEvent(g_shutdown_event);
    }
    if (g_flush_thread) {
        WaitForSingleObject(g_flush_thread, INFINITE);
        CloseHandle(g_flush_thread);
        g_flush_thread = NULL;
    }
    if (g_shutdown_event) {
        CloseHandle(g_shutdown_event);
        g_shutdown_event = NULL;
    }
    if (g_flush_event) {
        CloseHandle(g_flush_event);
        g_flush_event = NULL;
    }
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void TE_LogFlush(void) {
    if (g_flush_event) {
        SetEvent(g_flush_event);
    }
}

void TE_LogSetCallback(TE_LogFunc callback) {
    g_log_callback = callback;
}

void TE_LogWrite(TE_LogLevel level, const char* module, const char* message) {
    if (level < g_min_level) return;

    if (g_log_callback) {
        g_log_callback(level, module, message);
    }

    LONG pos;
    LONG next_pos;
    do {
        pos = g_write_pos;
        next_pos = pos + 1;
    } while (InterlockedCompareExchange(&g_write_pos, next_pos, pos) != pos);

    LONG index = pos & RING_BUFFER_MASK;
    LogEntry* entry = &g_ring_buffer[index];

    entry->level = level;
    GetLocalTime(&entry->timestamp);
    strncpy_s(entry->module, sizeof(entry->module), module ? module : "Unknown", _TRUNCATE);
    strncpy_s(entry->message, sizeof(entry->message), message ? message : "", _TRUNCATE);

    if (g_flush_event) {
        SetEvent(g_flush_event);
    }
}
