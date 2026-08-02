#pragma once
#include <sdk/te_plugin.h>
#include "magnification.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TE_TaskbarIcon {
    RECT bounds;
    wchar_t app_id[256];
    int icon_index;
} TE_TaskbarIcon;

typedef struct TE_IconHoverState {
    PluginContext ctx;
    
    /* Lifecycle flags (accessed via InterlockedExchange) */
    volatile LONG initialized;
    volatile LONG enabled;
    volatile LONG animating;
    
    /* Configuration (written on UI thread under exclusive lock) */
    float max_scale;
    int radius;
    int speed_ms;
    TE_MagnifyCurveType curve;
    
    /* Icon data (protected by SRWLock) */
    SRWLOCK icon_lock;
    TE_TaskbarIcon icons[TE_MAX_TASKBAR_ICONS];
    int icon_count;
    
    /* Per-frame scale/layout arrays (written by timer callback) */
    float scales[TE_MAX_TASKBAR_ICONS];
    float base_centers_x[TE_MAX_TASKBAR_ICONS];
    float displaced_x[TE_MAX_TASKBAR_ICONS];
    float displaced_y[TE_MAX_TASKBAR_ICONS];
    
    /* Settle animation state */
    float settle_start_scales[TE_MAX_TASKBAR_ICONS];
    LARGE_INTEGER settle_start_qpc;
    volatile LONG settling;
    bool was_in_taskbar;
    
    /* Taskbar geometry & multi-monitor cache */
    RECT taskbar_rect;
    float icon_size;
    HWND secondary_taskbars[8];
    uint32_t secondary_count;
} TE_IconHoverState;

#ifdef __cplusplus
}
#endif
