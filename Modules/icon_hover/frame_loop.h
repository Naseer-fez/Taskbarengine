#pragma once

#include "icon_hover_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start background frame timer loop for vsync magnification animation.
 */
HRESULT TE_FrameLoopStart(TE_IconHoverState* state);

/**
 * @brief Stop frame timer loop.
 */
void TE_FrameLoopStop(void);

/**
 * @brief Check if frame loop timer is currently running.
 */
bool TE_FrameLoopIsRunning(void);

#ifdef __cplusplus
}
#endif
