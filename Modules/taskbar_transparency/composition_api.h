#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Visual transparency modes supported by the composition engine.
 */
typedef enum TE_TransparencyMode {
    TE_MODE_CLEAR = 0,      /**< 100% transparent glass; wallpaper visible. */
    TE_MODE_BLUR = 1,       /**< Aero-style Gaussian blur behind taskbar. */
    TE_MODE_ACRYLIC = 2,    /**< Fluent Acrylic blur with noise/tint layer. */
    TE_MODE_TINT = 3,       /**< Custom color tint with adjustable opacity or solid color. */
    TE_MODE_DISABLED = 4    /**< Native Windows taskbar styling. */
} TE_TransparencyMode;

/**
 * Configuration parameters for the taskbar transparency module.
 */
typedef struct TE_TransparencySettings {
    bool enabled;               /**< Master toggle for transparency effect. */
    TE_TransparencyMode mode;   /**< Active visual mode. */
    float opacity;              /**< Normalized opacity [0.0f, 1.0f]. */
    uint32_t color_argb;        /**< Color in ARGB format (0xAARRGGBB or 0x00RRGGBB). */
    uint32_t accent_flags;      /**< Accent policy flags (e.g. 2 for acrylic luminance). */
    bool apply_secondary;       /**< Whether to apply to secondary monitor taskbars. */
} TE_TransparencySettings;

/**
 * Undocumented Windows DWM Accent States.
 */
typedef enum ACCENT_STATE {
    ACCENT_DISABLED = 0,                    /**< Default shell composition / opaque background. */
    ACCENT_ENABLE_GRADIENT = 1,              /**< Solid opaque gradient color. */
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,  /**< Transparent color gradient. */
    ACCENT_ENABLE_BLURBEHIND = 3,           /**< Classic Aero Gaussian blur behind. */
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,    /**< Fluent Acrylic blur behind (Win10 1803+, Win11). */
    ACCENT_ENABLE_HOSTBACKDROP = 5,         /**< System Mica/host backdrop. */
    ACCENT_INVALID_STATE = 6                /**< Sentinel/invalid state. */
} ACCENT_STATE;

/**
 * Undocumented Windows DWM Accent Policy structure.
 * Memory layout: 16 bytes (4 x 32-bit integers).
 */
typedef struct ACCENT_POLICY {
    ACCENT_STATE AccentState;   /**< Visual mode enum. */
    uint32_t AccentFlags;       /**< Bitmask controlling borders and color usage. */
    uint32_t GradientColor;     /**< Packed color in ABGR byte order (0xAABBGGRR). */
    uint32_t AnimationId;       /**< Internal animation identifier (typically 0). */
} ACCENT_POLICY;

/**
 * Undocumented Windows Composition Attributes.
 */
typedef enum WINDOWCOMPOSITIONATTRIB {
    WCA_UNDEFINED = 0,
    WCA_NCRENDERING_ENABLED = 1,
    WCA_NCRENDERING_POLICY = 2,
    WCA_TRANSITIONS_FORCEDISABLED = 3,
    WCA_ALLOW_NCPAINT = 4,
    WCA_CAPTION_BUTTON_BOUNDS = 5,
    WCA_NONCLIENT_RTL_LAYOUT = 6,
    WCA_FORCE_ICONIC_REPRESENTATION = 7,
    WCA_EXTENDED_FRAME_BOUNDS = 8,
    WCA_HAS_ICONIC_BITMAP = 9,
    WCA_THEME_ATTRIBUTES = 10,
    WCA_NCRENDERING_EXILED = 11,
    WCA_NCADORNMENTINFO = 12,
    WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
    WCA_VIDEO_OVERLAY_ACTIVE = 14,
    WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
    WCA_DISALLOW_PEEK = 16,
    WCA_CLOAK = 17,
    WCA_CLOAKED = 18,
    WCA_ACCENT_POLICY = 19,
    WCA_FREEZE_REPRESENTATION = 20,
    WCA_EVER_UNCLOAKED = 21,
    WCA_VISUAL_OWNER = 22,
    WCA_HOLOGRAPHIC = 23,
    WCA_EXCLUDED_FROM_DDA = 24,
    WCA_PASSIVEUPDATEMODE = 25,
    WCA_USEDARKMODECOLORS = 26,
    WCA_CORNER_STYLE = 27,
    WCA_PART_COLOR = 28,
    WCA_DISABLE_MOVESIZE_FEEDBACK = 29,
    WCA_LAST = 30
} WINDOWCOMPOSITIONATTRIB;

/**
 * Attribute data envelope passed to SetWindowCompositionAttribute.
 */
typedef struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib; /**< WCA_ACCENT_POLICY (19). */
    void* pvData;                   /**< Pointer to ACCENT_POLICY structure. */
    UINT cbData;                    /**< sizeof(ACCENT_POLICY) (16 bytes). */
} WINDOWCOMPOSITIONATTRIBDATA;

/**
 * Function pointer signature for user32!SetWindowCompositionAttribute.
 */
typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(
    HWND hWnd,
    WINDOWCOMPOSITIONATTRIBDATA* pAttrData
);

/* Initialization & Dynamic API Resolution */
bool TE_CompositionApiInit(void);
void TE_CompositionApiShutdown(void);
bool TE_CompositionIsAvailable(void);
void TE_CompositionSetApiFunction(pfnSetWindowCompositionAttribute fn);
pfnSetWindowCompositionAttribute TE_CompositionGetApiFunction(void);

/* Color Packing & Manipulation */
uint32_t TE_PackABGR(uint8_t a, uint8_t r, uint8_t g, uint8_t b);
uint32_t TE_ArgbToAbgr(uint32_t argb, float opacity);

/* Policy Calculation & Application */
ACCENT_POLICY TE_ComputeAccentPolicy(const TE_TransparencySettings* settings);
bool TE_ApplyAccentPolicy(HWND hwnd, const ACCENT_POLICY* policy);
bool TE_RestoreNativeAccent(HWND hwnd);

#ifdef __cplusplus
}
#endif
