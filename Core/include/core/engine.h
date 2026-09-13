#pragma once

#include <windows.h>
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** GetMessage hook procedure exported for SetWindowsHookEx injection.
 *  This is triggered by PostMessage(taskbar_hwnd, WM_NULL, 0, 0). */
#ifdef TE_ENGINE_EXPORTS
__declspec(dllexport)
#else
__declspec(dllimport)
#endif
LRESULT CALLBACK TE_GetMsgHookProc(int nCode, WPARAM wParam, LPARAM lParam);

/** CBT hook procedure exported for SetWindowsHookEx compatibility. */
#ifdef TE_ENGINE_EXPORTS
__declspec(dllexport)
#else
__declspec(dllimport)
#endif
LRESULT CALLBACK TE_CBTHookProc(int nCode, WPARAM wParam, LPARAM lParam);

/** Get the Engine DLL's HINSTANCE (stored during DllMain). */
HINSTANCE TE_EngineGetInstance(void);

/** Check if the current process is explorer.exe. */
BOOL TE_IsExplorerProcess(void);

#ifdef __cplusplus
}
#endif
