#ifndef TE_ICON_HOVER_INTERNAL_H
#define TE_ICON_HOVER_INTERNAL_H

/**
 * @file icon_hover_internal.h
 * @brief Shared internal state bridging C plugin ABI and C++ subsystems.
 *
 * This header defines the central state structure used by all IconHover
 * subsystems (UIA discovery, icon capture, DComp overlay, frame loop,
 * magnification). It is NOT part of the public API.
 */

#include <sdk/te_plugin.h>
#include "magnification.h"
#include "uia_discovery.h"
#include "dynamic_island.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of icons supported in the visual tree. */
#define TE_HOVER_MAX_ICONS TE_UIA_MAX_ICONS

/**
 * Configuration values for the IconHover plugin.
 * Parsed from the plugin's JSONC configuration section.
 */
typedef struct TE_HoverConfig {
    float max_scale;                /**< Peak magnification multiplier (1.0 - 2.0). */
    int radius;                     /**< Effect radius in pixels (40 - 300). */
    TE_MagnifyCurveType curve;      /**< Falloff curve type. */
    int speed_ms;                   /**< Settle animation duration in milliseconds (50 - 500). */
    int bounce_enabled;             /**< Non-zero if notification bounce is enabled. */
    float bounce_strength;          /**< Upward velocity magnitude for bounce impulse (200 - 1500). */
    int keep_on_top;                /**< Non-zero to ensure DComp overlay stays topmost above taskbar window. */
    int tilt_enabled;               /**< Non-zero if 3D tilt perspective is enabled. */
    float max_tilt_angle;           /**< Peak tilt angle in degrees (0 - 45). */
    int drag_drop_enabled;          /**< Non-zero if drag-and-drop drop zone physics is enabled. */
    float drag_recession_scale;     /**< Scale multiplier when icon is held/dragged (0.50 - 1.00). */
    float drop_zone_push;           /**< Horizontal push distance in pixels during drag (10 - 120). */
    wchar_t start_image_path[MAX_PATH]; /**< Custom start button image file path (PNG or SVG). */
    TE_DynamicIslandConfig dynamic_island; /**< Dynamic Island configuration. */
} TE_HoverConfig;

/**
 * Mouse tracking state for the hover animation.
 */
typedef struct TE_HoverMouseState {
    float cursor_x;                 /**< Current cursor X position (screen coords). */
    float cursor_y;                 /**< Current cursor Y position (screen coords). */
    int is_in_taskbar;              /**< Non-zero if mouse is currently inside taskbar bounds. */
    int is_dragging;                /**< Non-zero if mouse drag operation is active. */
    int is_settling;                /**< Non-zero if settle animation is in progress. */
    float settle_progress;          /**< Settle interpolation factor [0, 1]. */
    uint64_t last_mousemove_qpc;    /**< QPC timestamp of last WM_MOUSEMOVE received. */
} TE_HoverMouseState;

/**
 * Baseline vertical headroom in logical pixels at 96 DPI.
 * Accommodates upward expansion for icons up to 48px base height
 * at max_scale 2.0x, with antialiasing clearance.
 * Scaled at runtime via: (TE_HOVER_HEADROOM_BASE_PX * dpi) / 96
 */
#define TE_HOVER_HEADROOM_BASE_PX 1024

/**
 * Taskbar geometry information.
 */
typedef struct TE_TaskbarGeometryInfo {
    RECT taskbarRect;
    int taskbarHeight;
    int bridgeOffsetY;
    int baselineY;
    int headroom_y;
    uint64_t generation;
    int valid;
} TE_TaskbarGeometryInfo;

/**
 * Per-icon animation state.
 */
typedef struct TE_IconAnimState {
    float current_scale;            /**< Current interpolated scale factor. */
    float target_scale;             /**< Target scale computed from magnification math. */
    float center_x;                 /**< Icon center X in screen coordinates. */
    float center_y;                 /**< Icon center Y in screen coordinates. */
    float base_width;               /**< Base icon width (unscaled). */
    float base_height;              /**< Base icon height (unscaled). */
    uint64_t geometry_generation;   /**< Generation of geometry used for this animation. */
    float targetOffsetY;            /**< Target vertical offset (natural rest at 0.0f). */
    float currentOffsetY;           /**< Current vertical offset (negative = upward bounce). */
    float velocityOffsetY;          /**< Vertical velocity for spring physics oscillator. */
    float current_tilt_x;           /**< Current pitch tilt angle in radians. */
    float target_tilt_x;            /**< Target pitch tilt angle in radians. */
    float velocity_tilt_x;          /**< Pitch angular velocity for spring oscillator. */
    float current_tilt_y;           /**< Current yaw tilt angle in radians. */
    float target_tilt_y;            /**< Target yaw tilt angle in radians. */
    float velocity_tilt_y;          /**< Yaw angular velocity for spring oscillator. */
    float current_pos_x;            /**< Current smoothed horizontal offset for displacement. */
} TE_IconAnimState;

#define TE_MAX_MONITORS 16

/**
 * State for a single taskbar / monitor overlay instance.
 */
typedef struct TE_MonitorState {
    HWND taskbar_hwnd;                          /**< Target taskbar HWND (Shell_TrayWnd or Shell_SecondaryTrayWnd). */
    HMONITOR monitor;                           /**< Associated monitor handle. */
    HWND overlay_hwnd;                          /**< DComp overlay window handle. */
    uint32_t current_dpi;                       /**< Monitor DPI. */
    TE_TaskbarGeometryInfo geometry;            /**< Taskbar geometry on this display. */
    TE_IconElementCache icon_cache;             /**< Discovered icons on this taskbar. */
    TE_IconAnimState anim[TE_HOVER_MAX_ICONS];  /**< Per-icon animation state for this taskbar. */
    int anim_count;                             /**< Number of active icon animations. */
    TE_HoverMouseState mouse;                   /**< Mouse tracking state on this taskbar. */
    int target_index;                           /**< DirectComposition target index. */
    int is_active;                              /**< Non-zero if monitor is connected and active. */
} TE_MonitorState;

/**
 * Central internal state for the IconHover plugin.
 * Single instance, lifetime managed by icon_hover.c lifecycle.
 */
typedef struct TE_IconHoverState {
    const PluginContext* ctx;               /**< Host-provided plugin context. */
    SRWLOCK state_lock;                     /**< Thread-safe state access lock. */
    TE_HoverConfig config;                  /**< Current configuration. */
    TE_HoverMouseState mouse;               /**< Mouse tracking state. */
    TE_IconElementCache icon_cache;         /**< UIA-discovered icon elements. */
    TE_IconAnimState anim[TE_HOVER_MAX_ICONS]; /**< Per-icon animation state. */
    int anim_count;                         /**< Number of active icon animations. */
    HWND overlay_hwnd;                      /**< DComp overlay window handle. */
    TE_TaskbarGeometryInfo geometry;        /**< Cached taskbar geometry info. */
    uint32_t current_dpi;                   /**< Current monitor DPI. */
    int enabled;                            /**< Non-zero if plugin is actively running. */

    /* Multi-monitor array */
    TE_MonitorState monitors[TE_MAX_MONITORS];  /**< Per-monitor state array. */
    int monitor_count;                          /**< Total number of tracked monitors. */
} TE_IconHoverState;

/**
 * Global plugin state accessor.
 * Defined in icon_hover.c, used by all subsystem files.
 */
extern TE_IconHoverState g_hover_state;

/**
 * Accessor for the icon hover plugin interface.
 */
TE_EXPORT const PluginInterface* TE_IconHoverGetPluginInterface(void);

#ifdef __cplusplus
}
#endif

#endif /* TE_ICON_HOVER_INTERNAL_H */
