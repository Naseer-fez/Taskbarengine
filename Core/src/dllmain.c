#include "core/engine.h"
#include "core/engine_init.h"
#include "core/taskbar_subclass.h"

#include <windows.h>
#include <commctrl.h>

#include <sdk/te_types.h>

static HINSTANCE g_hinstDLL = NULL;
static HWND g_taskbarHwnd = NULL;

BOOL TE_IsExplorerProcess(void)
{
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0) return FALSE;
    
    const wchar_t* filename = wcsrchr(path, L'\\');
    filename = filename ? filename + 1 : path;
    
    return (_wcsicmp(filename, L"explorer.exe") == 0);
}

#include <stdio.h>

static void TE_WriteStartupError(HINSTANCE hInst, const char* msg) {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(hInst, path, MAX_PATH)) {
        wchar_t* last_slash = wcsrchr(path, L'\\');
        if (last_slash) {
            *(last_slash + 1) = L'\0';
            wcscat_s(path, MAX_PATH, L"startup_error.log");
            FILE* f = _wfopen(path, L"a");
            if (f) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
                fclose(f);
            }
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
        {
            if (!TE_IsExplorerProcess()) {
                return TRUE;
            }
            
            HMODULE hPin = NULL;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)DllMain, &hPin);

            DisableThreadLibraryCalls(hinstDLL);
            g_hinstDLL = hinstDLL;
            
            HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
            if (taskbar_hwnd) {
                g_taskbarHwnd = taskbar_hwnd;
                /* Install the full Phase 2 subclass proc instead of the minimal one.
                 * WM_TE_INIT will trigger TE_CoreManagerInit() on the UI thread. */
                TE_TaskbarSubclassInstall(taskbar_hwnd);
                PostMessage(taskbar_hwnd, WM_TE_INIT, 0, 0);
            } else {
                TE_WriteStartupError(hinstDLL, "CRITICAL ERROR: Shell_TrayWnd not found in Explorer process. Cannot initialize Core Manager.");
            }
            break;
        }
        case DLL_PROCESS_DETACH:
        {
            if (TE_IsExplorerProcess()) {
                if (g_taskbarHwnd) {
                    TE_TaskbarSubclassRemove(g_taskbarHwnd);
                    g_taskbarHwnd = NULL;
                }
                /* If lpvReserved != NULL, the process is terminating and worker
                 * threads are already terminated by the OS; waiting on them under
                 * loader lock causes an unrecoverable deadlock. */
                if (lpvReserved == NULL) {
                    TE_ShutdownEngine();
                }
            }
            break;
        }
    }
    return TRUE;
}

LRESULT CALLBACK TE_GetMsgHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    /* WH_GETMESSAGE hook proc: called on Explorer's message loop */
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK TE_CBTHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    /* WH_CBT hook proc: kept for compatibility */
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

HINSTANCE TE_EngineGetInstance(void)
{
    return g_hinstDLL;
}
