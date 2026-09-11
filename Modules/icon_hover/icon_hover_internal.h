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
} TE_HoverConfig;

/**
 * Mouse tracking state for the hover animation.
 */
typedef struct TE_HoverMouseState {
    float cursor_x;                 /**< Current cursor X position (screen coords). */
    float cursor_y;                 /**< Current cursor Y position (screen coords). */
    int is_in_taskbar;              /**< Non-zero if mouse is currently inside taskbar bounds. */
    int is_settling;                /**< Non-zero if settle animation is in progress. */
    float settle_progress;          /**< Settle interpolation factor [0, 1]. */
    uint64_t last_mousemove_qpc;    /**< QPC timestamp of last WM_MOUSEMOVE received. */
} TE_HoverMouseState;

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
} TE_IconAnimState;

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
    RECT taskbar_rect;                      /**< Cached screen rect of the taskbar. */
    int taskbar_height;                     /**< Current taskbar height from state store. */
    int headroom_y;                         /**< Headroom height above taskbar for unclipped growth. */
    uint32_t current_dpi;                   /**< Current monitor DPI. */
    int enabled;                            /**< Non-zero if plugin is actively running. */
} TE_IconHoverState;

/**
 * Global plugin state accessor.
 * Defined in icon_hover.c, used by all subsystem files.
 */
extern TE_IconHoverState g_hover_state;

#ifdef __cplusplus
}
#endif

#endif /* TE_ICON_HOVER_INTERNAL_H */
