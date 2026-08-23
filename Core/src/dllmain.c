#define TE_EXPORTS
#include "core/engine.h"
#include "core/core_manager.h"
#include "core/ipc_server.h"
#include <sdk/te_log.h>
#include <sdk/te_debug_trace.h>
#include <stdio.h>
#include <shlwapi.h>
#include <wchar.h>

#ifdef _MSC_VER
#pragma comment(lib, "Shlwapi.lib")
#endif

#define WM_TE_INIT (WM_APP + 100)

static HINSTANCE g_hinst_dll = NULL;

bool TE_IsExplorerProcess(void)
{
    wchar_t path[MAX_PATH];
    DWORD ret = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (ret == 0 || ret >= MAX_PATH) {
        return false;
    }

    const wchar_t* filename = PathFindFileNameW(path);
    return (_wcsicmp(filename, L"explorer.exe") == 0);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;

    switch (fdwReason) {
        case DLL_PROCESS_ATTACH: {
            g_hinst_dll = hinstDLL;
            TE_SetEngineInstance(hinstDLL);
            DisableThreadLibraryCalls(hinstDLL);
            /* OutputDebugStringA is safe under loader lock */
            OutputDebugStringA("[TE-DBG] DllMain: DLL_PROCESS_ATTACH\n");
            break;
        }

        case DLL_PROCESS_DETACH:
            OutputDebugStringA("[TE-DBG] DllMain: DLL_PROCESS_DETACH\n");
            /* No-op: Cleanup is handled by the IPC SHUTDOWN command sequence.
             * Calling TE_IpcServerStop / TE_CoreManagerShutdown here risks
             * deadlock under the loader lock (WaitForSingleObject on threads,
             * FreeLibrary on plugin DLLs, etc.).
             * See docs/design_decisions.md §Injection & Initialization. */
            break;
    }
    return TRUE;
}

static volatile LONG g_hook_fired = 0;

#define TE_ENGINE_INIT_TIMER_ID 0x5445494E /* 'TEIN' */

/**
 * @brief Timer callback that runs deferred engine initialization.
 *
 * Runs on the UI thread (Shell_TrayWnd's owner thread) AFTER the message
 * pump has had time to process XAML island startup messages.  This avoids
 * the previous failure mode where subclassing during a CBT hook callback
 * interrupted XAML layout and left the taskbar empty.
 *
 * @thread_safety Must run on the Shell_TrayWnd UI thread.
 */
static void CALLBACK TE_DeferredInitTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    (void)uMsg;
    (void)dwTime;
    KillTimer(hwnd, idEvent);
    TE_DebugTrace("[TE-DBG] DeferredInitTimerProc: Timer fired, calling TE_EngineInitialize\n");
    TE_EngineInitialize();
}

TE_API LRESULT CALLBACK TE_CbtHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    /* One-shot: only attempt initialization once per DLL load.
     * The CBT hook fires for every window operation on the targeted thread,
     * including hundreds of XAML island child-window creations during startup.
     * Calling TE_EngineInitialize directly from here interrupted XAML layout
     * and left the taskbar empty or caused Explorer to crash.
     *
     * Instead, schedule a 500ms timer on Shell_TrayWnd.  The timer callback
     * runs on the UI thread's message pump (via WM_TIMER), guaranteeing that
     * XAML has finished its initial layout before we subclass the window. */
    if (nCode >= 0 && InterlockedCompareExchange(&g_hook_fired, 1, 0) == 0) {
        if (TE_IsExplorerProcess()) {
            TE_DebugTrace("[TE-DBG] CBT: Is explorer, looking for Shell_TrayWnd...\n");
            HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
            if (taskbar) {
                TE_DebugTraceFmt("[TE-DBG] CBT: Found Shell_TrayWnd HWND=0x%p\n", (void*)taskbar);
                SetTimer(taskbar, TE_ENGINE_INIT_TIMER_ID, 500, TE_DeferredInitTimerProc);
                TE_DebugTrace("[TE-DBG] CBT: SetTimer(500ms) scheduled for deferred init\n");
            } else {
                TE_DebugTrace("[TE-DBG] CBT: Shell_TrayWnd NOT FOUND, resetting hook_fired for retry\n");
                /* Shell_TrayWnd doesn't exist yet — allow one retry on next
                 * CBT event.  This handles the rare case where the hook fires
                 * before the taskbar window is created during Explorer startup. */
                InterlockedExchange(&g_hook_fired, 0);
            }
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}
