#ifndef TE_DYNAMIC_ISLAND_H
#define TE_DYNAMIC_ISLAND_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for COM interfaces */
struct IDCompositionVisual;
struct IDCompositionDevice;
struct ID2D1Factory;

typedef struct TE_DynamicIslandConfig {
    BOOL enabled;
    int padding_tray;
    int compact_width;
    int expanded_width;
    int height;
    float corner_radius;
    int announce_duration_ms;
    int expand_duration_ms;
    int collapse_duration_ms;
    BOOL show_idle_pill;
} TE_DynamicIslandConfig;

/* Subsystem Lifecycle */
BOOL TE_DynamicIslandInit(const TE_DynamicIslandConfig* config);
BOOL TE_DynamicIslandEnable(HWND primary_taskbar, uint32_t dpi);
void TE_DynamicIslandDisable(void);
void TE_DynamicIslandShutdown(void);
void TE_DynamicIslandUpdateConfig(const TE_DynamicIslandConfig* config);
BOOL TE_DynamicIslandIsEnabled(void);
BOOL TE_DynamicIslandIsVisible(void);

/* Geometry & DPI Event Handlers */
void TE_DynamicIslandOnGeometryChanged(const RECT* taskbar_rect, HWND tray_hwnd);
void TE_DynamicIslandOnDpiChanged(uint32_t dpi);

/* Visual Tree Integration */
BOOL TE_DynamicIslandAttachVisualTree(struct IDCompositionVisual* root_visual, struct IDCompositionDevice* dcomp_device, struct ID2D1Factory* d2d_factory);
void TE_DynamicIslandDetachVisualTree(void);

/* Frame Loop Pacing & Mouse Interaction */
void TE_DynamicIslandUpdateFrame(float dt_sec);
void TE_DynamicIslandOnMouseMove(float cursor_x, float cursor_y);
void TE_DynamicIslandOnMouseLeave(void);
BOOL TE_DynamicIslandCheckClick(float cursor_x, float cursor_y);
BOOL TE_DynamicIslandIsSettled(void);
BOOL TE_DynamicIslandIsDirty(void);
void TE_DynamicIslandSetHeadroom(float headroom_y);

typedef void (*TE_DynamicIslandWakeCallback)(void);
void TE_DynamicIslandSetWakeCallback(TE_DynamicIslandWakeCallback callback);

/* Geometry & State Queries for Unit Testing & Verification */
BOOL TE_DynamicIslandGetBounds(RECT* out_screen_rect);
void TE_DynamicIslandGetVisualOffset(float* out_x, float* out_y);
float TE_DynamicIslandGetCurrentWidth(void);
float TE_DynamicIslandGetCurrentOpacity(void);

#ifdef __cplusplus
}

/* C++ only: mock media source injection for unit testing */
class IMediaSource;
void TE_DynamicIslandSetMediaSource(IMediaSource* source);
#endif

#endif /* TE_DYNAMIC_ISLAND_H */
