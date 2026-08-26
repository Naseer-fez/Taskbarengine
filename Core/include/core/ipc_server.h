#pragma once
#include <windows.h>
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_IpcServerStart(HWND taskbar_hwnd);
void TE_IpcServerStop(void);

#ifdef __cplusplus
}
#endif
