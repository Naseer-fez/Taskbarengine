#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the system tray icon */
HRESULT TE_TrayCreate(HWND hwnd_owner, UINT callback_msg);

/* Remove the system tray icon */
void TE_TrayDestroy(void);

#ifdef __cplusplus
}
#endif
