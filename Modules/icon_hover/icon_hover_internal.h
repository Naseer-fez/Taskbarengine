#pragma once

#include <sdk/te_plugin.h>
#include "magnification.h"
#include "uia_discovery.h"
#include "icon_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TE_IconHoverState {
    PluginContext ctx;
    bool initialized;
    bool enabled;
    bool animating;
    float max_scale;
    int radius;
    int speed_ms;
    TE_MagnifyCurveType curve;
    TE_TaskbarIcon icons[TE_MAX_TASKBAR_ICONS];
    int icon_count;
    float scales[TE_MAX_TASKBAR_ICONS];
} TE_IconHoverState;

#ifdef __cplusplus
}
#endif
