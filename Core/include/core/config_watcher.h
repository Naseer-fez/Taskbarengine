#pragma once

#include <sdk/te_types.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WM_TE_CONFIG_CHANGED (WM_APP + 101)

HRESULT TE_ConfigWatcherStart(const wchar_t* config_dir, HWND notify_hwnd);
void TE_ConfigWatcherStop(void);

#ifdef __cplusplus
}
#endif
