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
#include <objbase.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
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
        if (v >= 1.0f && v <= 10.0f) g_hover_state.config.max_scale = v;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "radius");
    if (cJSON_IsNumber(node)) {
        int v = node->valueint;
        if (v >= 40 && v <= 500) g_hover_state.config.radius = v;
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

    node = cJSON_GetObjectItemCaseSensitive(config, "bounce_enabled");
    if (cJSON_IsBool(node)) {
        g_hover_state.config.bounce_enabled = cJSON_IsTrue(node) ? 1 : 0;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "bounce_strength");
    if (cJSON_IsNumber(node)) {
        float v = (float)node->valuedouble;
        if (v >= 100.0f && v <= 2000.0f) g_hover_state.config.bounce_strength = v;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "tilt_enabled");
    if (cJSON_IsBool(node)) {
        g_hover_state.config.tilt_enabled = cJSON_IsTrue(node) ? 1 : 0;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "max_tilt_angle");
    if (cJSON_IsNumber(node)) {
        float v = (float)node->valuedouble;
        if (v >= 0.0f && v <= 45.0f) g_hover_state.config.max_tilt_angle = v;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "drag_drop_enabled");
    if (cJSON_IsBool(node)) {
        g_hover_state.config.drag_drop_enabled = cJSON_IsTrue(node) ? 1 : 0;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "drag_recession_scale");
    if (cJSON_IsNumber(node)) {
        float v = (float)node->valuedouble;
        if (v >= 0.3f && v <= 1.0f) g_hover_state.config.drag_recession_scale = v;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "drop_zone_push");
    if (cJSON_IsNumber(node)) {
        float v = (float)node->valuedouble;
        if (v >= 5.0f && v <= 200.0f) g_hover_state.config.drop_zone_push = v;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "keep_on_top");
    if (cJSON_IsBool(node)) {
        g_hover_state.config.keep_on_top = cJSON_IsTrue(node) ? 1 : 0;
    }

    node = cJSON_GetObjectItemCaseSensitive(config, "start_image_path");
    if (!node) node = cJSON_GetObjectItemCaseSensitive(config, "start_button_image");
    if (node && cJSON_IsString(node)) {
        if (node->valuestring && node->valuestring[0]) {
            size_t converted = 0;
            mbstowcs_s(&converted, g_hover_state.config.start_image_path, MAX_PATH, node->valuestring, _TRUNCATE);
        } else {
            g_hover_state.config.start_image_path[0] = L'\0';
        }
    } else if (!node) {
        g_hover_state.config.start_image_path[0] = L'\0';
    }

    /* Dynamic Island Configuration */
    const cJSON* di_node = cJSON_GetObjectItemCaseSensitive(config, "dynamic_island");
    if (di_node && cJSON_IsObject(di_node)) {
        node = cJSON_GetObjectItemCaseSensitive(di_node, "enabled");
        if (cJSON_IsBool(node)) {
            g_hover_state.config.dynamic_island.enabled = cJSON_IsTrue(node) ? TRUE : FALSE;
        }

        node = cJSON_GetObjectItemCaseSensitive(di_node, "padding_tray");
        if (cJSON_IsNumber(node)) {
            g_hover_state.config.dynamic_island.padding_tray = node->valueint;
        }

        node = cJSON_GetObjectItemCaseSensitive(di_node, "compact_width");
        if (cJSON_IsNumber(node)) {
            g_hover_state.config.dynamic_island.compact_width = node->valueint;
        }

        node = cJSON_GetObjectItemCaseSensitive(di_node, "expanded_width");
        if (cJSON_IsNumber(node)) {
            g_hover_state.config.dynamic_island.expanded_width = node->valueint;
        }

        node = cJSON_GetObjectItemCaseSensitive(di_node, "height");
        if (cJSON_IsNumber(node)) {
            g_hover_state.config.dynamic_island.height = node->valueint;
        }

        node = cJSON_GetObjectItemCaseSensitive(di_node, "corner_radius");
        if (cJSON_IsNumber(node)) {
            g_hover_state.config.dynamic_island.corner_radius = (float)node->valuedouble;
        }

        node = cJSON_GetObjectItemCaseSensitive(di_node, "announce_duration_ms");
        if (cJSON_IsNumber(node)) {
            g_hover_state.config.dynamic_island.announce_duration_ms = node->valueint;
        }

        node = cJSON_GetObjectItemCaseSensitive(di_node, "expand_duration_ms");
        if (cJSON_IsNumber(node)) {
            g_hover_state.config.dynamic_island.expand_duration_ms = node->valueint;
        }

        node = cJSON_GetObjectItemCaseSensitive(di_node, "collapse_duration_ms");
        if (cJSON_IsNumber(node)) {
            g_hover_state.config.dynamic_island.collapse_duration_ms = node->valueint;
        }
    }
}

/* ── Settings Schema ──────────────────────────────────────────────── */

static const char* s_curve_options[] = { "gaussian", "cubic", "cosine", "linear" };

static const SettingDescriptor g_settings[] = {
    {
        "max_scale", "Max Hover Scale",
        "Peak magnification multiplier when cursor is directly over an icon",
        TE_SETTING_FLOAT,
        { .float_val = { 1.3f, 1.0f, 10.0f, 0.05f } }
    },
    {
        "radius", "Effect Radius",
        "Distance in pixels over which neighboring icons are influenced",
        TE_SETTING_INT,
        { .int_val = { 120, 40, 500, 10 } }
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
    },
    {
        "bounce_enabled", "Enable Notification Bounce",
        "Whether icons perform an elastic spring bounce when receiving notifications",
        TE_SETTING_BOOL,
        { .bool_val = { TRUE } }
    },
    {
        "bounce_strength", "Bounce Impulse Strength",
        "Initial upward launch velocity applied when an app receives a notification",
        TE_SETTING_FLOAT,
        { .float_val = { 600.0f, 200.0f, 1500.0f, 25.0f } }
    },
    {
        "tilt_enabled", "Enable 3D Tilt Perspective",
        "DirectComposition 3D matrix transformation tilting icons toward cursor",
        TE_SETTING_BOOL,
        { .bool_val = { TRUE } }
    },
    {
        "max_tilt_angle", "Max Tilt Angle",
        "Maximum pitch and yaw rotation angle in degrees (0 to 45)",
        TE_SETTING_FLOAT,
        { .float_val = { 20.0f, 0.0f, 45.0f, 1.0f } }
    },
    {
        "drag_drop_enabled", "Enable Drag-and-Drop Physics",
        "Scale down held icon and widen drop target zone during drag",
        TE_SETTING_BOOL,
        { .bool_val = { TRUE } }
    },
    {
        "drag_recession_scale", "Drag Recession Scale",
        "Scale multiplier when icon is held or dragged (0.50 to 1.00)",
        TE_SETTING_FLOAT,
        { .float_val = { 0.80f, 0.50f, 1.0f, 0.05f } }
    },
    {
        "drop_zone_push", "Drop Zone Push Factor",
        "Horizontal parting distance in pixels for neighbor icons during drag (10 to 120)",
        TE_SETTING_FLOAT,
        { .float_val = { 50.0f, 10.0f, 120.0f, 5.0f } }
    },
    {
        "keep_on_top", "Keep Overlay On Top",
        "Continuously enforce HWND_TOPMOST so taskbar clicks do not obscure magnification",
        TE_SETTING_BOOL,
        { .bool_val = { TRUE } }
    }
};

static const PluginSettings g_plugin_settings = {
    g_settings,
    12
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
 * Updates the cached taskbarHeight in hover state.
 */
static void QueryTaskbarHeight(void) {
    if (!g_hover_state.ctx || !g_hover_state.ctx->query_state) return;

    StateValue val;
    HRESULT hr = g_hover_state.ctx->query_state("taskbar_resize.height", &val);
    if (TE_SUCCEEDED(hr) && val.type == TE_STATE_INT) {
        g_hover_state.geometry.taskbarHeight = val.data.int_val;
        HoverLog(TE_LOG_DEBUG, "Queried taskbar height: %d", g_hover_state.geometry.taskbarHeight);
    }
}

static void DiscoverTaskbars(HWND out_hwnds[TE_MAX_MONITORS], int* out_count) {
    int count = 0;
    HWND primary = (g_hover_state.ctx && g_hover_state.ctx->taskbar_hwnd)
                   ? g_hover_state.ctx->taskbar_hwnd
                   : FindWindowW(L"Shell_TrayWnd", NULL);
    if (primary && IsWindow(primary)) {
        out_hwnds[count++] = primary;
    }

    HWND sec = NULL;
    while ((sec = FindWindowExW(NULL, sec, L"Shell_SecondaryTrayWnd", NULL)) != NULL) {
        if (!IsWindow(sec)) continue;
        if (count < TE_MAX_MONITORS) {
            BOOL dup = FALSE;
            for (int i = 0; i < count; i++) {
                if (out_hwnds[i] == sec) { dup = TRUE; break; }
            }
            if (!dup) {
                out_hwnds[count++] = sec;
            }
        }
    }
    *out_count = count;
}

static void RebuildGeometryForMonitor(int m) {
    if (m < 0 || m >= TE_MAX_MONITORS) return;
    TE_MonitorState* mon = &g_hover_state.monitors[m];
    HWND tb = mon->taskbar_hwnd;
    if (!tb || !IsWindow(tb)) return;

    GetWindowRect(tb, &mon->geometry.taskbarRect);

    HWND bridge = FindWindowExA(tb, NULL, "Windows.UI.Composition.DesktopWindowContentBridge", NULL);
    if (bridge) {
        RECT bridgeRect;
        GetWindowRect(bridge, &bridgeRect);
        mon->geometry.bridgeOffsetY = bridgeRect.top - mon->geometry.taskbarRect.top;
    } else {
        mon->geometry.bridgeOffsetY = 0;
    }

    if (m == 0) {
        QueryTaskbarHeight();
    }
    mon->geometry.taskbarHeight = g_hover_state.geometry.taskbarHeight;
    mon->geometry.baselineY = mon->geometry.taskbarRect.bottom;
    uint32_t dpi = mon->current_dpi ? mon->current_dpi : 96;
    mon->geometry.headroom_y = (TE_HOVER_HEADROOM_BASE_PX * dpi) / 96;
    mon->geometry.generation++;
    mon->geometry.valid = 1;

    if (m == 0) {
        g_hover_state.geometry = mon->geometry;
    }
}

static void RebuildGeometry(void) {
    if (g_hover_state.monitor_count > 0) {
        for (int m = 0; m < g_hover_state.monitor_count; m++) {
            if (g_hover_state.monitors[m].is_active) {
                RebuildGeometryForMonitor(m);
            }
        }
    } else {
        HWND taskbar_hwnd = g_hover_state.ctx ? g_hover_state.ctx->taskbar_hwnd : NULL;
        if (!taskbar_hwnd) return;

        GetWindowRect(taskbar_hwnd, &g_hover_state.geometry.taskbarRect);

        HWND bridge = FindWindowExA(taskbar_hwnd, NULL, "Windows.UI.Composition.DesktopWindowContentBridge", NULL);
        if (bridge) {
            RECT bridgeRect;
            GetWindowRect(bridge, &bridgeRect);
            g_hover_state.geometry.bridgeOffsetY = bridgeRect.top - g_hover_state.geometry.taskbarRect.top;
        } else {
            g_hover_state.geometry.bridgeOffsetY = 0;
        }

        QueryTaskbarHeight();
        g_hover_state.geometry.baselineY = g_hover_state.geometry.taskbarRect.bottom;
        g_hover_state.geometry.headroom_y = (TE_HOVER_HEADROOM_BASE_PX * g_hover_state.current_dpi) / 96;
        g_hover_state.geometry.generation++;
        g_hover_state.geometry.valid = 1;
    }
}

#define WM_TE_ICONS_DISCOVERED (WM_APP + 142)
#define SUBCLASS_HOVER_ID 0x5448

typedef struct DiscoveryPayload {
    int monitor_index;
    HWND taskbar_hwnd;
    TE_IconElementCache cache;
    HBITMAP bitmaps[TE_HOVER_MAX_ICONS];
} DiscoveryPayload;

static HANDLE s_discovery_thread = NULL;
static HANDLE s_discovery_trigger_event = NULL;
static HANDLE s_discovery_stop_event = NULL;
static SRWLOCK s_discovery_lock = SRWLOCK_INIT;
static volatile DiscoveryPayload* s_pending_discovery[TE_MAX_MONITORS] = { 0 };

static DWORD WINAPI UiaDiscoveryWorkerThread(LPVOID lpParam);

static void TriggerAsyncUiaDiscovery(void) {
    AcquireSRWLockExclusive(&s_discovery_lock);
    if (!s_discovery_stop_event) {
        s_discovery_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    }
    if (!s_discovery_trigger_event) {
        s_discovery_trigger_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    }
    if (!s_discovery_thread) {
        s_discovery_thread = CreateThread(NULL, 0, UiaDiscoveryWorkerThread, NULL, 0, NULL);
    }
    if (s_discovery_trigger_event) {
        SetEvent(s_discovery_trigger_event);
    }
    ReleaseSRWLockExclusive(&s_discovery_lock);
}

static void FinishRebuildIconDataForMonitor(int m, const TE_IconElementCache* new_cache, const HBITMAP* bitmaps) {
    if (m < 0 || (g_hover_state.monitor_count > 0 && m >= g_hover_state.monitor_count)) return;
    TE_MonitorState* mon = (g_hover_state.monitor_count > 0) ? &g_hover_state.monitors[m] : &g_hover_state.monitors[0];
    HWND tb = mon->taskbar_hwnd;
    if (!tb && g_hover_state.ctx) tb = g_hover_state.ctx->taskbar_hwnd;
    if (!tb || !IsWindow(tb)) return;

    /* Save old animation state to preserve scales across rebuilds */
    TE_IconAnimState old_anim[TE_HOVER_MAX_ICONS];
    TE_IconElementInfo old_items[TE_HOVER_MAX_ICONS];
    int old_count = mon->anim_count;
    if (old_count > 0) {
        memcpy(old_anim, mon->anim, sizeof(TE_IconAnimState) * old_count);
        memcpy(old_items, mon->icon_cache.items, sizeof(TE_IconElementInfo) * old_count);
    }

    memcpy(&mon->icon_cache, new_cache, sizeof(TE_IconElementCache));

    uint32_t count = mon->icon_cache.count;
    if (count == 0) {
        HoverLog(TE_LOG_WARNING, "No taskbar icons discovered for monitor %d", m);
        return;
    }
    if (count > TE_HOVER_MAX_ICONS) count = TE_HOVER_MAX_ICONS;

    /* Initialize animation state from discovered bounds */
    mon->anim_count = (int)count;
    for (uint32_t i = 0; i < count; i++) {
        const RECT* b = &mon->icon_cache.items[i].buttonRect;
        float w = (float)(b->right - b->left);
        float h = (float)(b->bottom - b->top);

        float initial_scale = 1.0f;
        float initial_target = 1.0f;

        for (int j = 0; j < old_count; j++) {
            if (wcscmp(mon->icon_cache.items[i].app_id, old_items[j].app_id) == 0 ||
                abs(b->left - old_items[j].buttonRect.left) < 10) {
                initial_scale = old_anim[j].current_scale;
                initial_target = old_anim[j].target_scale;
                break;
            }
        }

        mon->anim[i].center_x = (float)b->left + w / 2.0f;
        mon->anim[i].center_y = (float)b->top + h / 2.0f;
        mon->anim[i].base_width = w;
        mon->anim[i].base_height = h;
        mon->anim[i].current_scale = initial_scale;
        mon->anim[i].target_scale = initial_target;
        mon->anim[i].geometry_generation = mon->geometry.generation + 1;
        mon->anim[i].targetOffsetY = 0.0f;
        mon->anim[i].currentOffsetY = 0.0f;
        mon->anim[i].velocityOffsetY = 0.0f;
        mon->anim[i].current_tilt_x = 0.0f;
        mon->anim[i].target_tilt_x = 0.0f;
        mon->anim[i].velocity_tilt_x = 0.0f;
        mon->anim[i].current_tilt_y = 0.0f;
        mon->anim[i].target_tilt_y = 0.0f;
        mon->anim[i].velocity_tilt_y = 0.0f;
        mon->anim[i].current_pos_x = 0.0f;
    }

    HBITMAP local_bitmaps[TE_HOVER_MAX_ICONS] = { 0 };
    if (!bitmaps) {
        for (uint32_t i = 0; i < count; i++) {
            if (mon->icon_cache.items[i].element_type != TE_ELEM_START_BUTTON) {
                TE_IconCaptureGetBitmapEx(
                    mon->icon_cache.items[i].app_id,
                    mon->icon_cache.items[i].icon_index,
                    &mon->icon_cache.items[i].glyphRect,
                    mon->icon_cache.items[i].hwnd,
                    mon->icon_cache.items[i].pid,
                    &local_bitmaps[i]
                );
            }
        }
        bitmaps = local_bitmaps;
    }

    /* Build/rebuild DComp visual tree for this target with pre-extracted bitmaps */
    int overlay_x = mon->geometry.taskbarRect.left + 1;
    int overlay_y = mon->geometry.taskbarRect.top - mon->geometry.headroom_y + 1;
    int baseline_y = mon->geometry.baselineY;

    if (g_hover_state.monitor_count > 0) {
        TE_DCompBuildVisualTreeForTarget(
            mon->target_index,
            (int)count,
            mon->icon_cache.items,
            bitmaps,
            baseline_y,
            overlay_x,
            overlay_y
        );
    } else {
        TE_DCompBuildVisualTree(
            (int)count,
            mon->icon_cache.items,
            bitmaps,
            baseline_y,
            overlay_x,
            overlay_y
        );
    }

    /* Atomically bump generation so next frame tick picks up new geometry (PERF-402) */
    InterlockedIncrement((volatile LONG*)&mon->geometry.generation);

    /* Mirror monitor 0 to global legacy state */
    if (m == 0) {
        g_hover_state.anim_count = mon->anim_count;
        memcpy(g_hover_state.anim, mon->anim, sizeof(TE_IconAnimState) * count);
        memcpy(&g_hover_state.icon_cache, &mon->icon_cache, sizeof(TE_IconElementCache));
        g_hover_state.geometry.generation = mon->geometry.generation;
    }

    HoverLog(TE_LOG_INFO, "Icon data rebuilt for monitor %d: %u icons (lockless swap)", m, count);
}

static DWORD WINAPI UiaDiscoveryWorkerThread(LPVOID lpParam) {
    (void)lpParam;
    /* Initialize COM strictly as MTA for worker thread (SYS-007 & PERF-404) */
    HRESULT hr_co = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    while (1) {
        HANDLE events[2] = { s_discovery_stop_event, s_discovery_trigger_event };
        if (!events[0] || !events[1]) break;

        DWORD wr = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (wr == WAIT_OBJECT_0) {
            break; /* Stop event signaled */
        }
        if (wr == WAIT_OBJECT_0 + 1) {
            if (!g_hover_state.enabled || WaitForSingleObject(s_discovery_stop_event, 0) == WAIT_OBJECT_0) {
                continue;
            }

            int mon_count = (g_hover_state.monitor_count > 0) ? g_hover_state.monitor_count : 1;
            for (int m = 0; m < mon_count; m++) {
                if (WaitForSingleObject(s_discovery_stop_event, 0) == WAIT_OBJECT_0 || !g_hover_state.enabled) {
                    break;
                }

                if (g_hover_state.monitor_count > 0 && !g_hover_state.monitors[m].is_active) continue;
                HWND tb = (g_hover_state.monitor_count > 0) ? g_hover_state.monitors[m].taskbar_hwnd
                                                            : (g_hover_state.ctx ? g_hover_state.ctx->taskbar_hwnd : NULL);
                if (!tb || !IsWindow(tb)) continue;

                DiscoveryPayload* payload = (DiscoveryPayload*)calloc(1, sizeof(DiscoveryPayload));
                if (!payload) continue;

                payload->monitor_index = m;
                payload->taskbar_hwnd = tb;

                /* Offload all UIA tree queries to MTA background worker thread (SYS-006 & PERF-401) */
                TE_UiaDiscoverIcons(tb, &payload->cache);

                if (WaitForSingleObject(s_discovery_stop_event, 0) == WAIT_OBJECT_0 || !g_hover_state.enabled) {
                    free(payload);
                    payload = NULL;
                    break;
                }

                /* Execute extraction asynchronously on background worker (PERF-402) */
                uint32_t count = payload->cache.count;
                if (count > TE_HOVER_MAX_ICONS) count = TE_HOVER_MAX_ICONS;
                for (uint32_t i = 0; i < count; i++) {
                    if (WaitForSingleObject(s_discovery_stop_event, 0) == WAIT_OBJECT_0 || !g_hover_state.enabled) {
                        free(payload);
                        payload = NULL;
                        break;
                    }

                    if (payload->cache.items[i].element_type == TE_ELEM_START_BUTTON) {
                        payload->bitmaps[i] = NULL;
                    } else {
                        TE_IconCaptureGetBitmapEx(
                            payload->cache.items[i].app_id,
                            payload->cache.items[i].icon_index,
                            &payload->cache.items[i].glyphRect,
                            payload->cache.items[i].hwnd,
                            payload->cache.items[i].pid,
                            &payload->bitmaps[i]
                        );
                    }
                }

                if (!payload) {
                    break;
                }

                if (WaitForSingleObject(s_discovery_stop_event, 0) == WAIT_OBJECT_0 || !g_hover_state.enabled) {
                    free(payload);
                    break;
                }

                /* Lockless structure swap into pending slot */
                DiscoveryPayload* old_p = (DiscoveryPayload*)InterlockedExchangePointer(
                    (PVOID*)&s_pending_discovery[m],
                    (PVOID)payload
                );
                if (old_p) {
                    free(old_p);
                }

                /* Post completion to UI thread */
                HWND notify_hwnd = (g_hover_state.monitor_count > 0) ? g_hover_state.monitors[0].taskbar_hwnd
                                                                     : (g_hover_state.ctx ? g_hover_state.ctx->taskbar_hwnd : NULL);
                if (notify_hwnd && IsWindow(notify_hwnd)) {
                    PostMessageW(notify_hwnd, WM_TE_ICONS_DISCOVERED, 0, (LPARAM)m);
                }
            }
        }
    }

    if (SUCCEEDED(hr_co)) {
        CoUninitialize();
    }
    return 0;
}

static LRESULT CALLBACK IconHoverSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass; (void)dwRefData;
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, IconHoverSubclassProc, uIdSubclass);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    if (msg == WM_TE_ICONS_DISCOVERED) {
        int m = (int)lParam;
        if (m >= 0 && m < TE_MAX_MONITORS) {
            /* Lockless structure swap (SYS-006 & PERF-401) */
            DiscoveryPayload* payload = (DiscoveryPayload*)InterlockedExchangePointer(
                (PVOID*)&s_pending_discovery[m],
                NULL
            );
            if (payload) {
                if (g_hover_state.enabled) {
                    /* Seamless swap into next frame tick without stopping frame loop (PERF-402) */
                    FinishRebuildIconDataForMonitor(payload->monitor_index, &payload->cache, payload->bitmaps);
                }
                free(payload);
            }
        }
        return 0;
    }
    if ((msg == WM_LBUTTONUP || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) && TE_DCompIsCustomStartButtonEnabled()) {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ClientToScreen(hwnd, &pt);
        RECT sb_bounds;
        if (TE_DCompGetStartButtonBounds(&sb_bounds) && PtInRect(&sb_bounds, pt)) {
            if (msg == WM_LBUTTONUP) {
                PostMessageW(hwnd, WM_SYSCOMMAND, SC_TASKLIST, 0);
            }
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/**
 * Rebuild icon discovery, capture, and visual tree across all active displays.
 * Completely asynchronous via dedicated MTA background worker thread.
 */
static void RebuildIconData(void) {
    TriggerAsyncUiaDiscovery();
}

#ifndef HSHELL_REDRAW
#define HSHELL_REDRAW 6
#endif
#ifndef HSHELL_FLASH
#define HSHELL_FLASH (HSHELL_REDRAW | 0x8000)
#endif

static int StrCaseContains(const wchar_t* haystack, const wchar_t* needle) {
    if (!haystack || !needle || !*haystack || !*needle) return 0;
    size_t h_len = wcslen(haystack);
    size_t n_len = wcslen(needle);
    if (n_len > h_len) return 0;
    for (size_t i = 0; i <= h_len - n_len; i++) {
        if (_wcsnicmp(haystack + i, needle, n_len) == 0) return 1;
    }
    return 0;
}

/**
 * Resolve an application HWND to its corresponding monitor and taskbar icon index.
 */
static int ResolveHwndToMonitorAndIcon(HWND hwnd, int* out_monitor_index, int* out_icon_index) {
    if (!hwnd || !IsWindow(hwnd)) return -1;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    wchar_t exe_path[MAX_PATH] = { 0 };
    wchar_t exe_name[MAX_PATH] = { 0 };
    wchar_t exe_base[MAX_PATH] = { 0 };
    wchar_t aumid[256] = { 0 };
    wchar_t window_title[256] = { 0 };

    if (pid != 0) {
        HANDLE h_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h_proc) {
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(h_proc, 0, exe_path, &size)) {
                const wchar_t* p = wcsrchr(exe_path, L'\\');
                if (p) {
                    wcscpy_s(exe_name, MAX_PATH, p + 1);
                } else {
                    wcscpy_s(exe_name, MAX_PATH, exe_path);
                }
                wcscpy_s(exe_base, MAX_PATH, exe_name);
                wchar_t* dot = wcsrchr(exe_base, L'.');
                if (dot) *dot = L'\0';
            }

            /* Query AUMID for packaged/UWP apps if possible */
            typedef LONG (WINAPI *pfnGetApplicationUserModelId)(HANDLE, UINT32*, PWSTR);
            HMODULE h_kernel = GetModuleHandleW(L"kernel32.dll");
            if (h_kernel) {
                pfnGetApplicationUserModelId pfnGetAUMID = 
                    (pfnGetApplicationUserModelId)(void*)GetProcAddress(h_kernel, "GetApplicationUserModelId");
                if (pfnGetAUMID) {
                    UINT32 aumid_len = 256;
                    pfnGetAUMID(h_proc, &aumid_len, aumid);
                }
            }

            CloseHandle(h_proc);
        }
    }

    GetWindowTextW(hwnd, window_title, 256);

    /* Search through monitor caches */
    int mon_count = (g_hover_state.monitor_count > 0) ? g_hover_state.monitor_count : 1;
    for (int m = 0; m < mon_count; m++) {
        const TE_IconElementCache* cache = (g_hover_state.monitor_count > 0)
                                         ? &g_hover_state.monitors[m].icon_cache
                                         : &g_hover_state.icon_cache;
        for (uint32_t i = 0; i < cache->count; i++) {
            const wchar_t* app_id = cache->items[i].app_id;
            if (!app_id || !*app_id) continue;

            if ((aumid[0] && StrCaseContains(app_id, aumid)) ||
                (exe_path[0] && StrCaseContains(app_id, exe_path)) ||
                (exe_name[0] && StrCaseContains(app_id, exe_name)) ||
                (exe_base[0] && StrCaseContains(app_id, exe_base)) ||
                (window_title[0] && (StrCaseContains(app_id, window_title) || StrCaseContains(window_title, app_id)))) {
                if (out_monitor_index) *out_monitor_index = m;
                if (out_icon_index) *out_icon_index = (int)i;
                return (int)i;
            }
        }
    }

    /* Fallback: if only one icon is managed, map to it */
    if (g_hover_state.icon_cache.count == 1) {
        if (out_monitor_index) *out_monitor_index = 0;
        if (out_icon_index) *out_icon_index = 0;
        return 0;
    }

    return -1;
}

static int ResolveHwndToIconIndex(HWND hwnd) {
    int m = 0, idx = -1;
    return ResolveHwndToMonitorAndIcon(hwnd, &m, &idx);
}

static UINT_PTR s_shell_hook_rebuild_timer = 0;

static VOID CALLBACK ShellHookDebounceProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    (void)hwnd; (void)uMsg; (void)dwTime;
    KillTimer(NULL, idEvent);
    s_shell_hook_rebuild_timer = 0;
    if (!g_hover_state.enabled) return;

    HoverLog(TE_LOG_INFO, "Debounced shell hook timer fired, triggering async icon discovery");
    for (int m = 0; m < g_hover_state.monitor_count; m++) {
        TE_UiaCacheInvalidate(&g_hover_state.monitors[m].icon_cache);
    }
    TE_UiaCacheInvalidate(&g_hover_state.icon_cache);

    /* Offload discovery to dedicated MTA background worker (SYS-006, PERF-401, PERF-402) */
    TriggerAsyncUiaDiscovery();
}

/**
 * Shell hook event handler — app opened/closed/activated or notifications.
 * Triggers icon re-discovery or inertial bouncing.
 */
static void OnShellHook(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;

    if (!g_hover_state.enabled) return;

    const TE_ShellHookData* hook_data = (const TE_ShellHookData*)data;
    if (!hook_data) return;

    int msg = hook_data->shell_msg;

    /* Check for flashing notification event (HSHELL_FLASH) */
    if (msg == HSHELL_FLASH || ((msg & 0x8000) && ((msg & 0x7FFF) == HSHELL_REDRAW))) {
        if (!g_hover_state.config.bounce_enabled) return;
        HWND flash_hwnd = hook_data->target_hwnd;
        int mon_index = 0, icon_index = -1;
        if (ResolveHwndToMonitorAndIcon(flash_hwnd, &mon_index, &icon_index) >= 0) {
            float strength = g_hover_state.config.bounce_strength > 0.0f ? g_hover_state.config.bounce_strength : 600.0f;
            HoverLog(TE_LOG_INFO, "Notification flash for HWND %p -> mon %d icon %d, triggering inertial bounce (strength=%.1f)",
                     (void*)flash_hwnd, mon_index, icon_index, strength);
            TE_FrameLoopTriggerIconBounceForMonitor(mon_index, icon_index, strength);
        } else {
            HoverLog(TE_LOG_DEBUG, "Notification flash for HWND %p could not be resolved to icon",
                     (void*)flash_hwnd);
        }
        return;
    }

    int base_msg = msg & 0x7FFF;
    /* Only rebuild icon cache when windows are actually created, destroyed, or replaced */
    if (base_msg != HSHELL_WINDOWCREATED &&
        base_msg != HSHELL_WINDOWDESTROYED &&
        base_msg != HSHELL_WINDOWREPLACED) {
        return;
    }

    /* Ignore shell hook messages originating from our own overlay windows */
    for (int m = 0; m < g_hover_state.monitor_count; m++) {
        if (g_hover_state.monitors[m].overlay_hwnd && hook_data->target_hwnd == g_hover_state.monitors[m].overlay_hwnd) {
            return;
        }
    }

    /* Fine-grained cache invalidation for the specific window (SYS-018 & PERF-403) */
    if (hook_data->target_hwnd) {
        TE_IconCaptureInvalidateHwnd(hook_data->target_hwnd);
    }

    /* Debounce rapid bursts of window creations/destructions (250ms quiet period) to prevent UI thread lock */
    if (s_shell_hook_rebuild_timer) {
        KillTimer(NULL, s_shell_hook_rebuild_timer);
        s_shell_hook_rebuild_timer = 0;
    }
    s_shell_hook_rebuild_timer = SetTimer(NULL, 0, 250, ShellHookDebounceProc);
}

/**
 * Config changed event handler — re-parse settings.
 */
static void OnConfigChanged(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    const TE_ConfigChangedData* changed = (const TE_ConfigChangedData*)data;
    ParseConfig((const cJSON*)changed->new_config);

    if (g_hover_state.config.start_image_path[0] != L'\0') {
        TE_DCompLoadStartImage(g_hover_state.config.start_image_path);
        if (TE_DCompIsCustomStartButtonEnabled()) {
            TE_UiaHideStartButtonAll(TRUE);
        } else {
            TE_UiaHideStartButtonAll(FALSE);
        }
    } else {
        TE_DCompSetCustomStartButtonEnabled(0);
        TE_UiaHideStartButtonAll(FALSE);
    }

    QueryTaskbarHeight();
    TE_DynamicIslandUpdateConfig(&g_hover_state.config.dynamic_island);
    if (g_hover_state.config.dynamic_island.enabled && !TE_DynamicIslandIsEnabled()) {
        HWND primary_tb = g_hover_state.monitors[0].taskbar_hwnd;
        TE_DynamicIslandEnable(primary_tb, g_hover_state.current_dpi);
    } else if (!g_hover_state.config.dynamic_island.enabled && TE_DynamicIslandIsEnabled()) {
        TE_DynamicIslandDisable();
    }
    RebuildIconData();
    HoverLog(TE_LOG_INFO, "Config updated: max_scale=%.2f, radius=%d, curve=%d, speed_ms=%d",
             g_hover_state.config.max_scale, g_hover_state.config.radius,
             (int)g_hover_state.config.curve, g_hover_state.config.speed_ms);
}

/**
 * Taskbar mouse movement / leave event handler.
 */
static void OnTaskbarMouse(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    if (!g_hover_state.enabled) return;

    const TE_TaskbarMouseData* mouse_data = (const TE_TaskbarMouseData*)data;
    if (!mouse_data) return;

    if (mouse_data->is_in_taskbar) {
        TE_FrameLoopOnMouseMoveEx((float)mouse_data->cursor_pos.x, (float)mouse_data->cursor_pos.y,
                                  mouse_data->is_dragging, mouse_data->taskbar_hwnd);
    } else {
        TE_FrameLoopOnMouseLeave();
    }
}

/**
 * Update monitor overlay windows and geometry when display or taskbar geometry changes.
 */
static void UpdateMonitors(void) {
    TE_FrameLoopStop();

    HWND discovered[TE_MAX_MONITORS] = { 0 };
    int count = 0;
    DiscoverTaskbars(discovered, &count);

    /* 1. Remove monitors whose taskbars are no longer present */
    for (int i = 1; i < g_hover_state.monitor_count; i++) {
        HWND tb = g_hover_state.monitors[i].taskbar_hwnd;
        BOOL found = FALSE;
        for (int j = 0; j < count; j++) {
            if (discovered[j] == tb) {
                found = TRUE;
                break;
            }
        }
        if (!found && g_hover_state.monitors[i].is_active) {
            if (g_hover_state.monitors[i].overlay_hwnd) {
                TE_DCompRemoveTarget(g_hover_state.monitors[i].target_index);
                TE_DCompDestroyOverlayWindow(g_hover_state.monitors[i].overlay_hwnd);
                g_hover_state.monitors[i].overlay_hwnd = NULL;
            }
            g_hover_state.monitors[i].is_active = 0;
        }
    }

    /* 2. Update existing monitors and add newly attached displays */
    for (int j = 0; j < count; j++) {
        HWND tb = discovered[j];
        int existing_idx = -1;
        for (int i = 0; i < g_hover_state.monitor_count; i++) {
            if (g_hover_state.monitors[i].taskbar_hwnd == tb) {
                existing_idx = i;
                break;
            }
        }

        if (existing_idx >= 0) {
            RebuildGeometryForMonitor(existing_idx);
            TE_MonitorState* mon = &g_hover_state.monitors[existing_idx];
            int ox = mon->geometry.taskbarRect.left + 1;
            int oy = mon->geometry.taskbarRect.top - mon->geometry.headroom_y + 1;
            int ow = mon->geometry.taskbarRect.right - mon->geometry.taskbarRect.left - 1;
            int oh = (mon->geometry.taskbarRect.bottom - mon->geometry.taskbarRect.top) + mon->geometry.headroom_y - 1;
            TE_DCompMoveOverlayWindow(mon->overlay_hwnd, ox, oy, ow, oh);
            mon->is_active = 1;
        } else if (g_hover_state.monitor_count < TE_MAX_MONITORS) {
            int new_idx = g_hover_state.monitor_count;
            TE_MonitorState* mon = &g_hover_state.monitors[new_idx];
            memset(mon, 0, sizeof(TE_MonitorState));
            mon->taskbar_hwnd = tb;
            mon->monitor = MonitorFromWindow(tb, MONITOR_DEFAULTTONEAREST);

            typedef UINT (WINAPI *pfnGetDpiForWindow)(HWND);
            HMODULE hUser = GetModuleHandleW(L"user32.dll");
            pfnGetDpiForWindow pfnDpi = hUser ? (pfnGetDpiForWindow)(void*)GetProcAddress(hUser, "GetDpiForWindow") : NULL;
            mon->current_dpi = pfnDpi ? pfnDpi(tb) : g_hover_state.current_dpi;
            if (!mon->current_dpi) mon->current_dpi = 96;

            RebuildGeometryForMonitor(new_idx);
            int ox = mon->geometry.taskbarRect.left + 1;
            int oy = mon->geometry.taskbarRect.top - mon->geometry.headroom_y + 1;
            int ow = mon->geometry.taskbarRect.right - mon->geometry.taskbarRect.left - 1;
            int oh = (mon->geometry.taskbarRect.bottom - mon->geometry.taskbarRect.top) + mon->geometry.headroom_y - 1;

            mon->overlay_hwnd = TE_DCompCreateOverlayWindow(tb, ox, oy, ow, oh);
            int target_idx = -1;
            HRESULT hr = TE_DCompAddTarget(tb, mon->overlay_hwnd, &target_idx);
            mon->target_index = target_idx;
            mon->is_active = (TE_SUCCEEDED(hr) && mon->overlay_hwnd != NULL);
            g_hover_state.monitor_count++;
        }
    }

    if (g_hover_state.monitor_count > 0 && g_hover_state.monitors[0].is_active) {
        g_hover_state.overlay_hwnd = g_hover_state.monitors[0].overlay_hwnd;
        g_hover_state.geometry = g_hover_state.monitors[0].geometry;
    }

    TE_IconCaptureInvalidate();
    RebuildIconData();
}

/**
 * Display DPI change event handler.
 */
static void OnDpiChanged(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    if (!g_hover_state.enabled) return;

    const TE_DpiChangedData* dpi_data = (const TE_DpiChangedData*)data;
    if (!dpi_data) return;

    HoverLog(TE_LOG_INFO, "DPI changed from %u to %u", dpi_data->old_dpi, dpi_data->new_dpi);
    g_hover_state.current_dpi = dpi_data->new_dpi;

    UpdateMonitors();
    TE_DynamicIslandSetHeadroom((float)g_hover_state.geometry.headroom_y);
    TE_DynamicIslandOnDpiChanged(dpi_data->new_dpi);
}

/**
 * Taskbar geometry change event handler.
 */
static void OnTaskbarGeometry(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)data; (void)user_data;
    if (!g_hover_state.enabled) return;

    UpdateMonitors();
    HWND tray = FindWindowExW(g_hover_state.monitors[0].taskbar_hwnd, NULL, L"TrayNotifyWnd", NULL);
    TE_DynamicIslandOnGeometryChanged(&g_hover_state.geometry.taskbarRect, tray);
}

/**
 * Display resolution / connected monitor change event handler.
 */
static void OnDisplayChanged(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)data; (void)user_data;
    if (!g_hover_state.enabled) return;

    HoverLog(TE_LOG_INFO, "Display layout changed, updating monitor overlays");
    UpdateMonitors();
}

/* ── Plugin Lifecycle ─────────────────────────────────────────────── */

static HRESULT Initialize(const PluginContext* ctx) {
    memset(&g_hover_state, 0, sizeof(g_hover_state));
    g_hover_state.ctx = ctx;
    InitializeSRWLock(&g_hover_state.state_lock);

    if (ctx && ctx->log) {
        TE_LogSetCallback(ctx->log);
    }

    /* Set defaults */
    g_hover_state.config.max_scale = 1.3f;
    g_hover_state.config.radius = 120;
    g_hover_state.config.curve = TE_CURVE_GAUSSIAN;
    g_hover_state.config.speed_ms = 150;
    g_hover_state.config.bounce_enabled = 1;
    g_hover_state.config.bounce_strength = 600.0f;
    g_hover_state.config.tilt_enabled = 1;
    g_hover_state.config.max_tilt_angle = 20.0f;
    g_hover_state.config.drag_drop_enabled = 1;
    g_hover_state.config.drag_recession_scale = 0.80f;
    g_hover_state.config.drop_zone_push = 50.0f;
    g_hover_state.config.keep_on_top = 1;
    g_hover_state.geometry.taskbarHeight = 48; /* Default until state store provides real value */

    g_hover_state.config.dynamic_island.enabled = FALSE;
    g_hover_state.config.dynamic_island.padding_tray = 12;
    g_hover_state.config.dynamic_island.compact_width = 80;
    g_hover_state.config.dynamic_island.expanded_width = 240;
    g_hover_state.config.dynamic_island.height = 30;
    g_hover_state.config.dynamic_island.corner_radius = 15.0f;
    g_hover_state.config.dynamic_island.announce_duration_ms = 3000;
    g_hover_state.config.dynamic_island.expand_duration_ms = 250;
    g_hover_state.config.dynamic_island.collapse_duration_ms = 250;

    g_hover_state.current_dpi = (ctx && ctx->dpi) ? ctx->dpi : 96;
    g_hover_state.geometry.headroom_y = (TE_HOVER_HEADROOM_BASE_PX * g_hover_state.current_dpi) / 96;
    if (ctx && ctx->taskbar_hwnd) {
        GetWindowRect(ctx->taskbar_hwnd, &g_hover_state.geometry.taskbarRect);
    }

    ParseConfig(ctx ? ctx->config : NULL);
    TE_DynamicIslandInit(&g_hover_state.config.dynamic_island);
    TE_DynamicIslandSetWakeCallback(TE_FrameLoopWakeDynamicIsland);
    HoverLog(TE_LOG_INFO, "Initialize: max_scale=%.2f, radius=%d, curve=%d, speed_ms=%d",
             g_hover_state.config.max_scale, g_hover_state.config.radius,
             (int)g_hover_state.config.curve, g_hover_state.config.speed_ms);

    return TE_S_OK;
}

static HRESULT Enable(void) {
    HoverLog(TE_LOG_INFO, "Enable: starting IconHover");

    /* Query current taskbar height from state store */
    QueryTaskbarHeight();

#ifndef TE_HOVER_TESTLIB
    /* Initialize icon capture subsystem */
    HRESULT hr = TE_IconCaptureInit();
    if (TE_FAILED(hr)) {
        HoverLog(TE_LOG_WARNING, "Icon capture init failed (non-fatal)");
    }
#endif

    /* Discover all connected taskbars (primary + secondary) */
    HWND discovered[TE_MAX_MONITORS] = { 0 };
    int tb_count = 0;
#ifndef TE_HOVER_TESTLIB
    DiscoverTaskbars(discovered, &tb_count);
#endif

    HWND primary = (tb_count > 0) ? discovered[0] : (g_hover_state.ctx ? g_hover_state.ctx->taskbar_hwnd : NULL);
    g_hover_state.current_dpi = g_hover_state.ctx ? (g_hover_state.ctx->dpi ? g_hover_state.ctx->dpi : 96) : 96;

    /* Setup monitor 0 (primary taskbar) */
    g_hover_state.monitors[0].taskbar_hwnd = primary;
    g_hover_state.monitors[0].monitor = primary ? MonitorFromWindow(primary, MONITOR_DEFAULTTOPRIMARY) : NULL;
    g_hover_state.monitors[0].current_dpi = g_hover_state.current_dpi;
    RebuildGeometryForMonitor(0);
#ifndef TE_HOVER_TESTLIB
    int tb_width = g_hover_state.geometry.taskbarRect.right - g_hover_state.geometry.taskbarRect.left;
    int tb_height = g_hover_state.geometry.taskbarRect.bottom - g_hover_state.geometry.taskbarRect.top;
    int overlay_x = g_hover_state.geometry.taskbarRect.left + 1;
    int overlay_y = g_hover_state.geometry.taskbarRect.top - g_hover_state.geometry.headroom_y + 1;
    int overlay_w = tb_width - 1;
    int overlay_h = tb_height + g_hover_state.geometry.headroom_y - 1;

    /* Create primary overlay window (WS_POPUP layered window with headroom) */
    g_hover_state.overlay_hwnd = TE_DCompCreateOverlayWindow(primary, overlay_x, overlay_y, overlay_w, overlay_h);
    if (!g_hover_state.overlay_hwnd) {
        HoverLog(TE_LOG_ERROR, "Failed to create overlay window — disabling IconHover");
        TE_IconCaptureShutdown();
        return TE_E_FAIL;
    }

    /* Initialize DirectComposition */
    HRESULT hr_dcomp = TE_DCompInitDevice(g_hover_state.overlay_hwnd);
    if (TE_FAILED(hr_dcomp)) {
        HoverLog(TE_LOG_ERROR, "DComp init failed — disabling IconHover gracefully");
        TE_DCompDestroyOverlayWindow(g_hover_state.overlay_hwnd);
        g_hover_state.overlay_hwnd = NULL;
        TE_IconCaptureShutdown();
        return TE_E_FAIL;
    }
#endif

    g_hover_state.monitors[0].overlay_hwnd = g_hover_state.overlay_hwnd;
    g_hover_state.monitors[0].target_index = 0;
    g_hover_state.monitors[0].is_active = 1;
    g_hover_state.monitor_count = 1;

    /* Setup secondary taskbars */
    for (int i = 1; i < tb_count; i++) {
        HWND sec_tb = discovered[i];
        TE_MonitorState* sec_mon = &g_hover_state.monitors[i];
        memset(sec_mon, 0, sizeof(TE_MonitorState));
        sec_mon->taskbar_hwnd = sec_tb;
        sec_mon->monitor = MonitorFromWindow(sec_tb, MONITOR_DEFAULTTONEAREST);

        typedef UINT (WINAPI *pfnGetDpiForWindow)(HWND);
        HMODULE hUser = GetModuleHandleW(L"user32.dll");
        pfnGetDpiForWindow pfnDpi = hUser ? (pfnGetDpiForWindow)(void*)GetProcAddress(hUser, "GetDpiForWindow") : NULL;
        sec_mon->current_dpi = pfnDpi ? pfnDpi(sec_tb) : g_hover_state.current_dpi;
        if (!sec_mon->current_dpi) sec_mon->current_dpi = 96;

        RebuildGeometryForMonitor(i);

#ifndef TE_HOVER_TESTLIB
        int sec_w = sec_mon->geometry.taskbarRect.right - sec_mon->geometry.taskbarRect.left;
        int sec_h = sec_mon->geometry.taskbarRect.bottom - sec_mon->geometry.taskbarRect.top;
        int sec_ox = sec_mon->geometry.taskbarRect.left + 1;
        int sec_oy = sec_mon->geometry.taskbarRect.top - sec_mon->geometry.headroom_y + 1;
        int sec_ow = sec_w - 1;
        int sec_oh = sec_h + sec_mon->geometry.headroom_y - 1;

        sec_mon->overlay_hwnd = TE_DCompCreateOverlayWindow(sec_tb, sec_ox, sec_oy, sec_ow, sec_oh);
        int target_idx = -1;
        HRESULT hr_sec = TE_DCompAddTarget(sec_tb, sec_mon->overlay_hwnd, &target_idx);
        sec_mon->target_index = target_idx;
        sec_mon->is_active = (TE_SUCCEEDED(hr_sec) && sec_mon->overlay_hwnd != NULL);
#else
        sec_mon->is_active = 1;
#endif
        g_hover_state.monitor_count++;
        HoverLog(TE_LOG_INFO, "Registered secondary taskbar %p", (void*)sec_tb);
    }

#ifndef TE_HOVER_TESTLIB
    /* Load custom start button image if specified and hide native Start buttons across all displays */
    if (g_hover_state.config.start_image_path[0] != L'\0') {
        TE_DCompLoadStartImage(g_hover_state.config.start_image_path);
        if (TE_DCompIsCustomStartButtonEnabled()) {
            TE_UiaHideStartButtonAll(TRUE);
        } else {
            TE_UiaHideStartButtonAll(FALSE);
        }
    } else {
        TE_DCompSetCustomStartButtonEnabled(0);
        TE_UiaHideStartButtonAll(FALSE);
    }

    /* Subclass primary taskbar to receive UIA discovery messages */
    if (primary) {
        SetWindowSubclass(primary, IconHoverSubclassProc, SUBCLASS_HOVER_ID, 0);
    }

    /* Discover icons asynchronously via dedicated MTA background worker thread */
    TriggerAsyncUiaDiscovery();

    /* Subscribe to engine events */
    g_hover_state.ctx->subscribe(TE_EVENT_SHELL_HOOK, OnShellHook, NULL);
    g_hover_state.ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged, NULL);
    g_hover_state.ctx->subscribe(TE_EVENT_TASKBAR_MOUSE, OnTaskbarMouse, NULL);
    g_hover_state.ctx->subscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged, NULL);
    g_hover_state.ctx->subscribe(TE_EVENT_TASKBAR_GEOMETRY, OnTaskbarGeometry, NULL);
    g_hover_state.ctx->subscribe(TE_EVENT_DISPLAY_CHANGED, OnDisplayChanged, NULL);

    /* Register for mouse tracking via message filter (v2 API) */
    if (TE_CTX_HAS_FIELD(g_hover_state.ctx, subscribe_message) &&
        g_hover_state.ctx->subscribe_message) {
        g_hover_state.ctx->subscribe_message(WM_MOUSEMOVE);
        g_hover_state.ctx->subscribe_message(WM_MOUSELEAVE);
    }
#endif

    TE_DynamicIslandSetHeadroom((float)g_hover_state.geometry.headroom_y);
    TE_DynamicIslandSetWakeCallback(TE_FrameLoopWakeDynamicIsland);
    if (g_hover_state.config.dynamic_island.enabled) {
        TE_DynamicIslandEnable(primary, g_hover_state.current_dpi);
    }

    g_hover_state.enabled = 1;
    HoverLog(TE_LOG_INFO, "Enable complete: %d monitors, primary icons %d, overlay ready",
             g_hover_state.monitor_count, g_hover_state.anim_count);

    return TE_S_OK;
}

static HRESULT Disable(void) {
    HoverLog(TE_LOG_INFO, "Disable: stopping IconHover");
    if (s_shell_hook_rebuild_timer) {
        KillTimer(NULL, s_shell_hook_rebuild_timer);
        s_shell_hook_rebuild_timer = 0;
    }
    TE_DynamicIslandDisable();
    g_hover_state.enabled = 0;

    /* Stop animation */
    TE_FrameLoopStop();

#ifndef TE_HOVER_TESTLIB
    /* Restore native start button across all displays */
    TE_UiaHideStartButtonAll(FALSE);
#endif

    /* Unsubscribe message filters */
    if (TE_CTX_HAS_FIELD(g_hover_state.ctx, unsubscribe_message) &&
        g_hover_state.ctx->unsubscribe_message) {
        g_hover_state.ctx->unsubscribe_message(WM_MOUSEMOVE);
        g_hover_state.ctx->unsubscribe_message(WM_MOUSELEAVE);
    }

    /* Unsubscribe events */
    g_hover_state.ctx->unsubscribe(TE_EVENT_SHELL_HOOK, OnShellHook);
    g_hover_state.ctx->unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged);
    g_hover_state.ctx->unsubscribe(TE_EVENT_TASKBAR_MOUSE, OnTaskbarMouse);
    g_hover_state.ctx->unsubscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged);
    g_hover_state.ctx->unsubscribe(TE_EVENT_TASKBAR_GEOMETRY, OnTaskbarGeometry);
    g_hover_state.ctx->unsubscribe(TE_EVENT_DISPLAY_CHANGED, OnDisplayChanged);

    if (g_hover_state.monitors[0].taskbar_hwnd) {
        RemoveWindowSubclass(g_hover_state.monitors[0].taskbar_hwnd, IconHoverSubclassProc, SUBCLASS_HOVER_ID);
    }

    /* Destroy secondary overlay windows and targets */
    for (int i = 1; i < g_hover_state.monitor_count; i++) {
        if (g_hover_state.monitors[i].overlay_hwnd) {
            TE_DCompRemoveTarget(g_hover_state.monitors[i].target_index);
            TE_DCompDestroyOverlayWindow(g_hover_state.monitors[i].overlay_hwnd);
            g_hover_state.monitors[i].overlay_hwnd = NULL;
        }
        g_hover_state.monitors[i].is_active = 0;
    }

    /* Tear down primary DComp and overlay */
    TE_DCompDestroyDevice();
    if (g_hover_state.overlay_hwnd) {
        TE_DCompDestroyOverlayWindow(g_hover_state.overlay_hwnd);
        g_hover_state.overlay_hwnd = NULL;
    }
    g_hover_state.monitors[0].overlay_hwnd = NULL;
    g_hover_state.monitors[0].is_active = 0;
    g_hover_state.monitor_count = 0;

    /* Terminate MTA background discovery thread and flush pending payloads */
    AcquireSRWLockExclusive(&s_discovery_lock);
    if (s_discovery_stop_event) {
        SetEvent(s_discovery_stop_event);
    }
    if (s_discovery_thread) {
        WaitForSingleObject(s_discovery_thread, 3000);
        CloseHandle(s_discovery_thread);
        s_discovery_thread = NULL;
    }
    if (s_discovery_trigger_event) {
        CloseHandle(s_discovery_trigger_event);
        s_discovery_trigger_event = NULL;
    }
    if (s_discovery_stop_event) {
        CloseHandle(s_discovery_stop_event);
        s_discovery_stop_event = NULL;
    }
    ReleaseSRWLockExclusive(&s_discovery_lock);
    for (int m = 0; m < TE_MAX_MONITORS; m++) {
        DiscoveryPayload* p = (DiscoveryPayload*)InterlockedExchangePointer(
            (PVOID*)&s_pending_discovery[m],
            NULL
        );
        if (p) free(p);
    }

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
    TE_DynamicIslandShutdown();
    TE_FrameLoopShutdown();
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

TE_EXPORT const PluginInterface* TE_IconHoverGetPluginInterface(void) {
    return &g_interface;
}

TE_EXPORT void TE_IconHoverTriggerAsyncDiscovery(void) {
    TriggerAsyncUiaDiscovery();
}

#ifndef TE_HOVER_TESTLIB
TE_EXPORT const PluginInterface* GetPluginInterface(void) {
    return TE_IconHoverGetPluginInterface();
}
#endif
