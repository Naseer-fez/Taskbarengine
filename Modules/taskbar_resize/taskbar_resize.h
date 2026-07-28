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

#ifdef __cplusplus
}
#endif
