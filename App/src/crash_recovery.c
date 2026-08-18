#include "app/crash_recovery.h"
#include <sdk/te_debug_trace.h>

static HANDLE g_thread = NULL;
static HANDLE g_stop_event = NULL;
static HWND g_hwnd = NULL;
static UINT g_taskbar_created_msg = 0;
static volatile LONG g_state = TE_CRASH_RECOVERY_RUNNING;

TE_CrashRecoveryState TE_CrashRecoveryAdvance(TE_CrashRecoveryState state, UINT msg, bool explorer_dead)
{
    if (state == TE_CRASH_RECOVERY_RUNNING && explorer_dead) return TE_CRASH_RECOVERY_EXPLORER_DEAD;
    if (state == TE_CRASH_RECOVERY_EXPLORER_DEAD) return TE_CRASH_RECOVERY_WAITING_TASKBAR_CREATED;
    if (state == TE_CRASH_RECOVERY_WAITING_TASKBAR_CREATED && msg == g_taskbar_created_msg) return TE_CRASH_RECOVERY_REHOOKING;
    if (state == TE_CRASH_RECOVERY_REHOOKING) return TE_CRASH_RECOVERY_RUNNING;
    return state;
}

static DWORD WINAPI RecoveryThreadProc(LPVOID param)
{
    DWORD explorer_pid = (DWORD)(uintptr_t)param;
    HANDLE explorer = OpenProcess(SYNCHRONIZE, FALSE, explorer_pid);
    if (!explorer) return 0;

    HANDLE waits[2] = { g_stop_event, explorer };
    DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (wait == WAIT_OBJECT_0 + 1) {
        InterlockedExchange(&g_state, TE_CRASH_RECOVERY_WAITING_TASKBAR_CREATED);
        TE_DebugTrace("[TE-DBG] CrashRecovery: Explorer process exited, waiting for TaskbarCreated\n");
        /* Message-only windows do not receive broadcast TaskbarCreated
         * notifications.  Explicitly notify the tray host after Explorer
         * exits so it can reinstall the hook for the replacement process. */
        if (g_hwnd && g_taskbar_created_msg) {
            PostMessageW(g_hwnd, g_taskbar_created_msg, 0, 0);
        }
    }

    CloseHandle(explorer);
    return 0;
}

HRESULT TE_CrashRecoveryStart(HWND hwnd, DWORD explorer_pid, TE_ReinstallHookFunc reinstall_hook, void* context)
{
    (void)reinstall_hook;
    (void)context;
    if (g_thread) return S_OK;

    g_hwnd = hwnd;
    g_taskbar_created_msg = RegisterWindowMessageW(L"TaskbarCreated");
    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_stop_event) return HRESULT_FROM_WIN32(GetLastError());

    g_thread = CreateThread(NULL, 0, RecoveryThreadProc, (LPVOID)(uintptr_t)explorer_pid, 0, NULL);
    if (!g_thread) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
        return hr;
    }

    InterlockedExchange(&g_state, TE_CRASH_RECOVERY_RUNNING);
    return S_OK;
}

void TE_CrashRecoveryStop(void)
{
    if (g_stop_event) SetEvent(g_stop_event);
    if (g_thread) {
        WaitForSingleObject(g_thread, 1000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
    }
}

TE_CrashRecoveryState TE_CrashRecoveryGetState(void)
{
    return (TE_CrashRecoveryState)InterlockedCompareExchange(&g_state, 0, 0);
}
