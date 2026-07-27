#define TE_EXPORTS
#include "core/engine.h"
#include <sdk/te_log.h>
#include <shlwapi.h>
#include <wchar.h>

#ifdef _MSC_VER
#pragma comment(lib, "Shlwapi.lib")
#endif

#define WM_TE_INIT (WM_APP + 100)

static HINSTANCE g_hinst_dll = NULL;
static volatile LONG g_init_once = 0;

static bool TE_IsExplorerProcess(void)
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

            if (TE_IsExplorerProcess()) {
                HWND tray_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
                if (tray_hwnd != NULL) {
                    PostMessageW(tray_hwnd, WM_TE_INIT, 0, 0);
                }
            }
            break;

        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

TE_API LRESULT CALLBACK TE_CbtHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (TE_IsExplorerProcess() && InterlockedCompareExchange(&g_init_once, 1, 0) == 0) {
        TE_EngineInitialize();
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}
