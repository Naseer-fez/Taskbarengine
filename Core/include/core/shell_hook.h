#pragma once
#include <windows.h>
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_ShellHookInit(HWND taskbar_hwnd);
void TE_ShellHookShutdown(HWND taskbar_hwnd);
UINT TE_ShellHookGetMessageId(void);
void TE_ShellHookProcess(WPARAM wParam, LPARAM lParam);

#ifdef __cplusplus
}
#endif
