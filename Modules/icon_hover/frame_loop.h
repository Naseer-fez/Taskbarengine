#pragma once
#include "icon_hover_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_FrameLoopStart(TE_IconHoverState* state);
void TE_FrameLoopStop(void);
HRESULT TE_FrameLoopActivate(void);
void TE_FrameLoopDeactivate(void);
void TE_FrameLoopUpdateMouse(int x, int y);

#ifdef __cplusplus
}
#endif
