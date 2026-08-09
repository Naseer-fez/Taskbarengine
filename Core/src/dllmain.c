#define TE_EXPORTS
#include "core/engine.h"
#include "core/core_manager.h"
#include "core/ipc_server.h"
#include <sdk/te_log.h>
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
        case DLL_PROCESS_ATTACH:
            g_hinst_dll = hinstDLL;
            TE_SetEngineInstance(hinstDLL);
            DisableThreadLibraryCalls(hinstDLL);
            break;

        case DLL_PROCESS_DETACH:
            /* No-op: Cleanup is handled by the IPC SHUTDOWN command sequence.
             * Calling TE_IpcServerStop / TE_CoreManagerShutdown here risks
             * deadlock under the loader lock (WaitForSingleObject on threads,
             * FreeLibrary on plugin DLLs, etc.).
             * See docs/design_decisions.md §Injection & Initialization. */
            break;
    }
    return TRUE;
}

TE_API LRESULT CALLBACK TE_CbtHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    if (nCode >= 0 && TE_IsExplorerProcess()) {
        TE_EngineInitialize();
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}
