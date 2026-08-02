#pragma once
#include <windows.h>
#include "icon_capture.h"
#include "icon_hover_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_DcompInit(HWND parent_hwnd);
HRESULT TE_DcompUpdateVisuals(TE_IconHoverState* state);
void TE_DcompShutdown(void);

#ifdef __cplusplus
}
#endif
