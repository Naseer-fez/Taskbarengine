#pragma once

#include <windows.h>
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Phase B initialization — called on the UI thread via WM_TE_INIT.
 *  Finds Shell_TrayWnd, logs success via OutputDebugStringW.
 *  This runs OUTSIDE the loader lock, so it is safe to call
 *  LoadLibrary, CreateThread, COM, etc. */
HRESULT TE_InitializeEngine(HWND taskbar_hwnd);

/** Shutdown the engine — called during cleanup.
 *  Reverts all changes and releases resources. */
void TE_ShutdownEngine(void);

#ifdef __cplusplus
}
#endif
