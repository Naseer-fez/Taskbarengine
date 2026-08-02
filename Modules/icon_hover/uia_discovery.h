#pragma once
#include "icon_hover_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_UiaDiscoverIcons(HWND taskbar_hwnd, TE_TaskbarIcon* out_icons, int max_count, int* out_count);
void TE_UiaCleanup(void);

#ifdef __cplusplus
}
#endif
