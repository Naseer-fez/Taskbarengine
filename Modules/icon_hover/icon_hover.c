/**
 * @file icon_hover.c
 * @brief IconHover plugin ABI entry points (pure C).
 *
 * Implements the PluginInterface lifecycle for macOS Dock-style icon
 * hover magnification. Bridges the C plugin ABI with C++ subsystems
 * (UIA discovery, icon capture, DComp overlay, frame loop).
 *
 * Priority: 110 (visual overlay layer, loads after taskbar_resize at 10).
 */

#include <sdk/te_plugin.h>
#include <sdk/te_events.h>
#include <sdk/te_jsonc.h>
#include <cJSON.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "icon_hover_internal.h"
#include "magnification.h"
#include "uia_discovery.h"
#include "icon_capture.h"
#include "dcomp_overlay.h"
#include "frame_loop.h"

/* ── Global State ─────────────────────────────────────────────────── */

TE_IconHoverState g_hover_state = { 0 };

/* ── Logging Helper ───────────────────────────────────────────────── */

static void HoverLog(TE_LogLevel level, const char* fmt, ...) {
    if (g_hover_state.ctx && g_hover_state.ctx->log) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        g_hover_state.ctx->log(level, "IconHover", buf);
    }
}

/* ── Configuration ────────────────────────────────────────────────── */

static const char* s_curve_names[] = { "gaussian", "cubic", "cosine", "linear" };

static TE_MagnifyCurveType ParseCurveType(const char* name) {
    if (!name) return TE_CURVE_GAUSSIAN;
    for (int i = 0; i < 4; i++) {
        if (_stricmp(name, s_curve_names[i]) == 0) {
            return (TE_MagnifyCurveType)i;
        }
    }
    return TE_CURVE_GAUSSIAN;
}

static void ParseConfig(const cJSON* config) {
    if (!config) return;

    const cJSON* node;

    node = cJSON_GetObjectItemCaseSensitive(config, "max_scale");
    if (!node) node = cJSON_GetObjectItemCaseSensitive(config, "scale");
    if (cJSON_IsNumber(node)) {
        float v = (float)node->valuedouble;
        if (v >= 1.0f && v <= 2.0f) g_hover_state.config.max_scale = v;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "radius");
    if (cJSON_IsNumber(node)) {
        int v = node->valueint;
        if (v >= 40 && v <= 300) g_hover_state.config.radius = v;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "curve");
    if (cJSON_IsString(node) && node->valuestring) {
        g_hover_state.config.curve = ParseCurveType(node->valuestring);
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "speed_ms");
    if (cJSON_IsNumber(node)) {
        int v = node->valueint;
        if (v >= 50 && v <= 500) g_hover_state.config.speed_ms = v;
    }
}

/* ── Settings Schema ──────────────────────────────────────────────── */

static const char* s_curve_options[] = { "gaussian", "cubic", "cosine", "linear" };

static const SettingDescriptor g_settings[] = {
    {
        "max_scale", "Max Hover Scale",
        "Peak magnification multiplier when cursor is directly over an icon",
        TE_SETTING_FLOAT,
        { .float_val = { 1.3f, 1.0f, 2.0f, 0.05f } }
    },
    {
        "radius", "Effect Radius",
        "Distance in pixels over which neighboring icons are influenced",
        TE_SETTING_INT,
        { .int_val = { 120, 40, 300, 10 } }
    },
    {
        "curve", "Easing Curve",
        "Mathematical curve controlling the magnification falloff profile",
        TE_SETTING_ENUM,
        { .enum_val = { 0, s_curve_options, 4 } }
    },
    {
        "speed_ms", "Animation Duration",
        "Duration in milliseconds for the settle animation when mouse leaves",
        TE_SETTING_INT,
        { .int_val = { 150, 50, 500, 10 } }
    }
};

static const PluginSettings g_plugin_settings = {
    g_settings,
    4
};

/* ── Plugin Metadata ──────────────────────────────────────────────── */

static const PluginMetadata g_metadata = {
    "icon_hover",
    "Icon Hover",
    "macOS Dock-style icon magnification with DirectComposition GPU-accelerated animation.",
    "TaskbarEngine",
    100,    /* version 1.0.0 */
    110,    /* priority: visual overlay layer (100-199 band) */
    TE_API_VERSION
};

/* ── Event Handlers ───────────────────────────────────────────────── */

/**
 * Query taskbar_resize.height from the shared state store.
 * Updates the cached taskbar_height in hover state.
 */
static void QueryTaskbarHeight(void) {
    if (!g_hover_state.ctx || !g_hover_state.ctx->query_state) return;

    StateValue val;
    HRESULT hr = g_hover_state.ctx->query_state("taskbar_resize.height", &val);
    if (TE_SUCCEEDED(hr) && val.type == TE_STATE_INT) {
        g_hover_state.taskbar_height = val.data.int_val;
        HoverLog(TE_LOG_DEBUG, "Queried taskbar height: %d", g_hover_state.taskbar_height);
    }
}

/**
 * Rebuild icon discovery, capture, and visual tree.
 * Called on initial enable and when apps change (shell hook).
 */
static void RebuildIconData(void) {
    HWND taskbar_hwnd = g_hover_state.ctx->taskbar_hwnd;
    if (!taskbar_hwnd) return;

    /* Stop timer to prevent concurrent DComp tree modification */
    int was_active = TE_FrameLoopIsActive();
    if (was_active) {
        TE_FrameLoopStop();
    }

    /* Discover icons via UIA */
    TE_UiaCacheInvalidate(&g_hover_state.icon_cache);
    HRESULT hr = TE_UiaDiscoverIcons(taskbar_hwnd, &g_hover_state.icon_cache);
    if (TE_FAILED(hr)) {
        HoverLog(TE_LOG_WARNING, "UIA icon discovery failed");
        if (was_active) TE_FrameLoopStart();
        return;
    }

    uint32_t count = g_hover_state.icon_cache.count;
    if (count == 0) {
        HoverLog(TE_LOG_WARNING, "No taskbar icons discovered");
        if (was_active) TE_FrameLoopStart();
        return;
    }

    /* Initialize animation state from discovered bounds */
    g_hover_state.anim_count = (int)count;
    for (uint32_t i = 0; i < count; i++) {
        const RECT* b = &g_hover_state.icon_cache.items[i].bounds;
        float w = (float)(b->right - b->left);
        float h = (float)(b->bottom - b->top);

        g_hover_state.anim[i].center_x = (float)b->left + w / 2.0f;
        g_hover_state.anim[i].center_y = (float)b->top + h / 2.0f;
        g_hover_state.anim[i].base_width = w;
        g_hover_state.anim[i].base_height = h;
        g_hover_state.anim[i].current_scale = 1.0f;
        g_hover_state.anim[i].target_scale = 1.0f;
    }

    /* Capture icon bitmaps */
    HBITMAP bitmaps[TE_HOVER_MAX_ICONS] = { 0 };
    for (uint32_t i = 0; i < count; i++) {
        TE_IconCaptureGetBitmap(
            g_hover_state.icon_cache.items[i].app_id,
            g_hover_state.icon_cache.items[i].icon_index,
            &bitmaps[i]
        );
    }

    /* Build/rebuild DComp visual tree */
    RECT bounds[TE_HOVER_MAX_ICONS];
    for (uint32_t i = 0; i < count; i++) {
        /* Convert screen coords to overlay-local coords */
        RECT screen_bounds = g_hover_state.icon_cache.items[i].bounds;
        POINT pt = { screen_bounds.left, screen_bounds.top };
        if (g_hover_state.overlay_hwnd) {
            ScreenToClient(g_hover_state.overlay_hwnd, &pt);
        }
        bounds[i].left = pt.x;
        bounds[i].top = pt.y;
        bounds[i].right = pt.x + (screen_bounds.right - screen_bounds.left);
        bounds[i].bottom = pt.y + (screen_bounds.bottom - screen_bounds.top);
    }

    TE_DCompBuildVisualTree((int)count, bounds, bitmaps);

    if (was_active) {
        TE_FrameLoopStart();
    }

    HoverLog(TE_LOG_INFO, "Icon data rebuilt: %u icons", count);
}

/**
 * Shell hook event handler — app opened/closed/activated.
 * Triggers icon re-discovery.
 */
static void OnShellHook(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)data; (void)user_data;

    if (!g_hover_state.enabled) return;

    HoverLog(TE_LOG_DEBUG, "Shell hook received, invalidating icon cache");
    TE_IconCaptureInvalidate();
    RebuildIconData();
}

/**
 * Config changed event handler — re-parse settings.
 */
static void OnConfigChanged(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    const TE_ConfigChangedData* changed = (const TE_ConfigChangedData*)data;
    ParseConfig((const cJSON*)changed->new_config);
    QueryTaskbarHeight();
    HoverLog(TE_LOG_INFO, "Config updated: max_scale=%.2f, radius=%d, curve=%d, speed_ms=%d",
             g_hover_state.config.max_scale, g_hover_state.config.radius,
             (int)g_hover_state.config.curve, g_hover_state.config.speed_ms);
}

/* ── Plugin Lifecycle ─────────────────────────────────────────────── */

static HRESULT Initialize(const PluginContext* ctx) {
    memset(&g_hover_state, 0, sizeof(g_hover_state));
    g_hover_state.ctx = ctx;

    /* Set defaults */
    g_hover_state.config.max_scale = 1.3f;
    g_hover_state.config.radius = 120;
    g_hover_state.config.curve = TE_CURVE_GAUSSIAN;
    g_hover_state.config.speed_ms = 150;
    g_hover_state.taskbar_height = 48; /* Default until state store provides real value */

    ParseConfig(ctx->config);
    HoverLog(TE_LOG_INFO, "Initialize: max_scale=%.2f, radius=%d, curve=%d, speed_ms=%d",
             g_hover_state.config.max_scale, g_hover_state.config.radius,
             (int)g_hover_state.config.curve, g_hover_state.config.speed_ms);

    return TE_S_OK;
}

static HRESULT Enable(void) {
    HoverLog(TE_LOG_INFO, "Enable: starting IconHover");

    /* Query current taskbar height from state store */
    QueryTaskbarHeight();

    /* Initialize icon capture subsystem */
    HRESULT hr = TE_IconCaptureInit();
    if (TE_FAILED(hr)) {
        HoverLog(TE_LOG_WARNING, "Icon capture init failed (non-fatal)");
    }

    /* Get taskbar dimensions for overlay */
    HWND taskbar_hwnd = g_hover_state.ctx->taskbar_hwnd;
    RECT taskbar_rect;
    GetWindowRect(taskbar_hwnd, &taskbar_rect);
    int tb_width = taskbar_rect.right - taskbar_rect.left;
    int tb_height = taskbar_rect.bottom - taskbar_rect.top;

    /* Create overlay window */
    g_hover_state.overlay_hwnd = TE_DCompCreateOverlayWindow(taskbar_hwnd, tb_width, tb_height);
    if (!g_hover_state.overlay_hwnd) {
        HoverLog(TE_LOG_ERROR, "Failed to create overlay window — disabling IconHover");
        TE_IconCaptureShutdown();
        return TE_E_FAIL;
    }

    /* Initialize DirectComposition */
    hr = TE_DCompInitDevice(g_hover_state.overlay_hwnd);
    if (TE_FAILED(hr)) {
        HoverLog(TE_LOG_ERROR, "DComp init failed — disabling IconHover gracefully");
        TE_DCompDestroyOverlayWindow(g_hover_state.overlay_hwnd);
        g_hover_state.overlay_hwnd = NULL;
        TE_IconCaptureShutdown();
        return TE_E_FAIL;
    }

    /* Discover icons and build visual tree */
    RebuildIconData();

    /* Subscribe to events */
    g_hover_state.ctx->subscribe(TE_EVENT_SHELL_HOOK, OnShellHook, NULL);
    g_hover_state.ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged, NULL);

    /* Register for mouse tracking via message filter (v2 API) */
    if (TE_CTX_HAS_FIELD(g_hover_state.ctx, subscribe_message) &&
        g_hover_state.ctx->subscribe_message) {
        g_hover_state.ctx->subscribe_message(WM_MOUSEMOVE);
        g_hover_state.ctx->subscribe_message(WM_MOUSELEAVE);
    }

    g_hover_state.enabled = 1;
    HoverLog(TE_LOG_INFO, "Enable complete: %d icons, overlay ready", g_hover_state.anim_count);

    return TE_S_OK;
}

static HRESULT Disable(void) {
    HoverLog(TE_LOG_INFO, "Disable: stopping IconHover");
    g_hover_state.enabled = 0;

    /* Stop animation */
    TE_FrameLoopStop();

    /* Unsubscribe message filters */
    if (TE_CTX_HAS_FIELD(g_hover_state.ctx, unsubscribe_message) &&
        g_hover_state.ctx->unsubscribe_message) {
        g_hover_state.ctx->unsubscribe_message(WM_MOUSEMOVE);
        g_hover_state.ctx->unsubscribe_message(WM_MOUSELEAVE);
    }

    /* Unsubscribe events */
    g_hover_state.ctx->unsubscribe(TE_EVENT_SHELL_HOOK, OnShellHook);
    g_hover_state.ctx->unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged);

    /* Tear down DComp */
    TE_DCompDestroyDevice();
    TE_DCompDestroyOverlayWindow(g_hover_state.overlay_hwnd);
    g_hover_state.overlay_hwnd = NULL;

    /* Release icon cache */
    TE_IconCaptureShutdown();

    HoverLog(TE_LOG_INFO, "Disable complete");
    return TE_S_OK;
}

static HRESULT Update(float delta_time) {
    (void)delta_time;
    /* Animation runs on timer thread via frame_loop, not the UI Update tick */
    return TE_S_OK;
}

static HRESULT Shutdown(void) {
    HoverLog(TE_LOG_INFO, "Shutdown called");
    g_hover_state.ctx = NULL;
    return TE_S_OK;
}

static const PluginMetadata* GetMetadata(void) {
    return &g_metadata;
}

static const PluginSettings* GetSettings(void) {
    return &g_plugin_settings;
}

/* ── Plugin Interface (frozen ABI) ────────────────────────────────── */

static const PluginInterface g_interface = {
    Initialize,
    Enable,
    Disable,
    Update,
    Shutdown,
    GetMetadata,
    GetSettings
};

TE_EXPORT const PluginInterface* GetPluginInterface(void) {
    return &g_interface;
}
