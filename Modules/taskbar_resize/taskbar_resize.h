#pragma once

#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_TASKBAR_RESIZE_MIN_HEIGHT 24
#define TE_TASKBAR_RESIZE_MAX_HEIGHT 72
#define TE_TASKBAR_RESIZE_DEFAULT_HEIGHT 40

int TE_TaskbarResizeClampHeight(int height);
int TE_TaskbarResizeScaleForDpi(int value, uint32_t dpi);

/**
 * Modifies a WINDOWPOS struct to enforce the target height at the given DPI.
 * Used by the WM_WINDOWPOSCHANGING handler. Exposed for unit testing.
 */
void TE_TaskbarResizeEnforceWindowPos(WINDOWPOS* wp, int target_height, uint32_t dpi);

#ifdef __cplusplus
}
#endif
