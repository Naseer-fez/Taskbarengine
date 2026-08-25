#pragma once

#include <windows.h>
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CBT hook procedure exported for SetWindowsHookEx injection.
 *  This is the entry point called by Windows when a CBT event occurs. */
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
