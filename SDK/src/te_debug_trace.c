#include <sdk/te_debug_trace.h>
#include <stdio.h>
#include <stdarg.h>

/* Fixed trace file path — writable by Explorer */
static const char* TRACE_FILE = "C:\\Users\\Public\\te_debug_trace.log";
static HANDLE g_trace_mutex = NULL;
static volatile LONG g_trace_init = 0;

static void EnsureInit(void)
{
    if (InterlockedCompareExchange(&g_trace_init, 1, 0) == 0) {
        g_trace_mutex = CreateMutexA(NULL, FALSE, "Global\\TE_DebugTraceMutex");
    }
}

void TE_DebugTrace(const char* msg)
{
    if (!msg) return;
    OutputDebugStringA(msg);

    EnsureInit();

    if (g_trace_mutex) {
        WaitForSingleObject(g_trace_mutex, 500);
    }

    HANDLE hfile = CreateFileA(TRACE_FILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hfile != INVALID_HANDLE_VALUE) {
        /* Timestamp */
        SYSTEMTIME st;
        GetLocalTime(&st);
        char ts[64];
        int ts_len = sprintf(ts, "[%02d:%02d:%02d.%03d] ",
                             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        DWORD written;
        WriteFile(hfile, ts, (DWORD)ts_len, &written, NULL);
        WriteFile(hfile, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(hfile);
    }

    if (g_trace_mutex) {
        ReleaseMutex(g_trace_mutex);
    }
}

void TE_DebugTraceFmt(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    TE_DebugTrace(buf);
}
