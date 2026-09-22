/**
 * @file frame_loop.cpp
 * @brief High-refresh scanout-aligned animation timer and DComp commit loop for IconHover.
 *
 * Drives the magnification, spring bounce, 3D tilt, and dynamic island animations:
 *   1. Paced dynamically to monitor refresh rates (60 Hz, 120 Hz, 144 Hz, 240 Hz)
 *      via DwmGetCompositionTimingInfo and CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (PERF-103).
 *   2. Dedicated worker thread with explicit COM MTA apartment initialization (SYS-009).
 *   3. Lockless double-buffered snapshot architecture for tear-free physics reads (PERF-101).
 *   4. Stationary mouse hover detection settling to 0.0% CPU (PERF-104).
 *   5. Geometry generation latching across frame boundaries without frame drops (PERF-105).
 */

#include "frame_loop.h"
#include "icon_hover_internal.h"
#include "magnification.h"
#include "dcomp_overlay.h"
#include "dynamic_island.h"
#include <sdk/te_log.h>

#include <windows.h>
#include <objbase.h>
#include <dwmapi.h>
#include <math.h>
#include <atomic>
#include <algorithm>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static const char* LOG_TAG = "FrameLoop";

/**
 * Double-buffered input snapshot for concurrency isolation (PERF-101).
 */
struct TE_MouseSnapshot {
    float cursor_x;
    float cursor_y;
    int is_in_taskbar;
    int is_dragging;
    int is_settling;
    float settle_progress;
    uint64_t last_mousemove_qpc;
};

struct TE_MonitorDoubleBuffer {
    TE_MouseSnapshot mouse_snapshots[2];
    std::atomic<int> active_idx{0};
    std::atomic<float> pending_impulses[TE_HOVER_MAX_ICONS];
};

static TE_MonitorDoubleBuffer s_monitor_buffers[TE_MAX_MONITORS];

/** Thread, timer, and event handles. */
static HANDLE s_thread = NULL;
static HANDLE s_waitable_timer = NULL;
static HANDLE s_wake_event = NULL;
static HANDLE s_stop_event = NULL;
static HANDLE s_shutdown_event = NULL;
static volatile LONG s_loop_active = 0;
static thread_local bool s_is_callback_thread = false;
static bool s_worker_initialized = false;
static int s_is_high_res = 0;

/** QPC frequency for delta time calculation. */
static LARGE_INTEGER s_qpc_freq = {};
static uint64_t s_last_tick_qpc = 0;

/** Stationary hover cursor history (PERF-104). */
static float s_prev_cursor_x[TE_MAX_MONITORS] = { 0 };
static float s_prev_cursor_y[TE_MAX_MONITORS] = { 0 };
static bool s_has_prev_cursor[TE_MAX_MONITORS] = { false };

/* Forward declarations */
static VOID CALLBACK FrameTimerCallback(PVOID lpParam, BOOLEAN timer_or_wait_fired);
static void EnsureWorkerInitialized(void);

double TE_FrameLoopGetRefreshRate(void)
{
    DWM_TIMING_INFO timing_info = {};
    timing_info.cbSize = sizeof(timing_info);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &timing_info)) &&
        timing_info.rateRefresh.uiDenominator > 0 &&
        timing_info.rateRefresh.uiNumerator > 0) {
        double rate = (double)timing_info.rateRefresh.uiNumerator / (double)timing_info.rateRefresh.uiDenominator;
        if (rate >= 24.0 && rate <= 500.0) {
            return rate;
        }
    }

    HDC hdc = GetDC(NULL);
    if (hdc) {
        int vrefresh = GetDeviceCaps(hdc, VREFRESH);
        ReleaseDC(NULL, hdc);
        if (vrefresh > 1 && vrefresh <= 500) {
            return (double)vrefresh;
        }
    }

    return 60.0;
}

int TE_FrameLoopIsTimerHighResolution(void)
{
    return s_is_high_res;
}

/**
 * Compute displaced X positions when icons magnify or during drag-and-drop.
 * If dragging, icons part significantly wider to create an expansive drop zone.
 */
static void ComputeDisplacedPositions(
    const TE_IconAnimState* anim, int count,
    float cursor_x, int is_dragging, int is_in_taskbar, float radius,
    float drop_zone_push,
    float* out_pos_x, float* out_pos_y)
{
    if (count <= 0) return;

    float push_factor = is_dragging ? (drop_zone_push > 0.0f ? drop_zone_push : 50.0f) : 15.0f;
    float influence_radius = radius > 0.0f ? radius : 120.0f;

    for (int i = 0; i < count; i++) {
        out_pos_y[i] = anim ? anim[i].currentOffsetY : 0.0f;

        if (!is_in_taskbar) {
            out_pos_x[i] = 0.0f;
            continue;
        }

        float dx = anim[i].center_x - cursor_x;
        float dist = fabsf(dx);

        if (dist < influence_radius && dist > 0.5f) {
            float falloff = 1.0f - (dist / influence_radius);
            float sign = (dx > 0.0f) ? 1.0f : -1.0f;

            if (is_dragging) {
                // In drag mode: hovered icon is held down, neighbor icons part wider to form drop zone
                if (dist < 15.0f) {
                    out_pos_x[i] = 0.0f;
                } else {
                    out_pos_x[i] = sign * push_factor * falloff;
                }
            } else {
                out_pos_x[i] = sign * push_factor * falloff;
            }
        } else {
            out_pos_x[i] = 0.0f;
        }
    }
}

/**
 * Process a single monitor's animation tick:
 * physics (spring bounce, 3D tilt), magnification math, horizontal displacement,
 * and DComp transform updates.
 *
 * Returns true if this monitor's animation is completely settled at rest.
 */
static bool ProcessMonitorTick(
    TE_MonitorState* mon,
    int monitor_idx,
    const TE_HoverConfig* config,
    float dt,
    uint64_t now_qpc,
    POINT cur,
    bool has_cur,
    bool* out_is_dirty)
{
    (void)now_qpc;
    (void)cur;
    (void)has_cur;
    int icon_count = mon->anim_count;
    if (icon_count <= 0) {
        if (out_is_dirty) *out_is_dirty = false;
        return true;
    }

    /* PERF-101: Consume any pending bounce impulses locklessly */
    if (monitor_idx >= 0 && monitor_idx < TE_MAX_MONITORS) {
        for (int i = 0; i < icon_count; i++) {
            float imp = s_monitor_buffers[monitor_idx].pending_impulses[i].exchange(0.0f, std::memory_order_acq_rel);
            if (imp > 0.0f) {
                mon->anim[i].velocityOffsetY = -imp;
                mon->anim[i].targetOffsetY = 0.0f;
            }
        }
    }

    /* PERF-101: Read double-buffered mouse snapshot locklessly */
    if (monitor_idx >= 0 && monitor_idx < TE_MAX_MONITORS) {
        int read_idx = s_monitor_buffers[monitor_idx].active_idx.load(std::memory_order_acquire);
        const TE_MouseSnapshot& snap = s_monitor_buffers[monitor_idx].mouse_snapshots[read_idx];
        if (snap.last_mousemove_qpc > mon->mouse.last_mousemove_qpc) {
            mon->mouse.cursor_x = snap.cursor_x;
            mon->mouse.cursor_y = snap.cursor_y;
            mon->mouse.is_in_taskbar = snap.is_in_taskbar;
            mon->mouse.is_dragging = snap.is_dragging;
            mon->mouse.is_settling = snap.is_settling;
            mon->mouse.settle_progress = snap.settle_progress;
            mon->mouse.last_mousemove_qpc = snap.last_mousemove_qpc;
        }
    }

    /* PERF-104: Stationary cursor velocity detection */
    bool cursor_stationary = false;
    if (monitor_idx >= 0 && monitor_idx < TE_MAX_MONITORS) {
        if (s_has_prev_cursor[monitor_idx]) {
            float cdx = mon->mouse.cursor_x - s_prev_cursor_x[monitor_idx];
            float cdy = mon->mouse.cursor_y - s_prev_cursor_y[monitor_idx];
            float dist_sq = cdx * cdx + cdy * cdy;
            if (dist_sq < 0.01f) {
                cursor_stationary = true;
            }
        }
        s_prev_cursor_x[monitor_idx] = mon->mouse.cursor_x;
        s_prev_cursor_y[monitor_idx] = mon->mouse.cursor_y;
        s_has_prev_cursor[monitor_idx] = true;
    }

    float old_scales[TE_HOVER_MAX_ICONS];
    float old_pos_x[TE_HOVER_MAX_ICONS];
    float old_pos_y[TE_HOVER_MAX_ICONS];
    float old_tilt_x[TE_HOVER_MAX_ICONS];
    float old_tilt_y[TE_HOVER_MAX_ICONS];
    for (int i = 0; i < icon_count; i++) {
        old_scales[i] = mon->anim[i].current_scale;
        old_pos_x[i] = mon->anim[i].current_pos_x;
        old_pos_y[i] = mon->anim[i].currentOffsetY;
        old_tilt_x[i] = mon->anim[i].current_tilt_x;
        old_tilt_y[i] = mon->anim[i].current_tilt_y;
    }



    /* PERF-105: Latch geometry generation changes atomically across frame boundaries */
    uint64_t current_geom_gen = mon->geometry.generation;
    for (int i = 0; i < icon_count; i++) {
        if (mon->anim[i].geometry_generation != current_geom_gen) {
            mon->anim[i].geometry_generation = current_geom_gen;

            /* Apply immediate position clamping so visuals smoothly interpolate without drops */
            float max_headroom = (float)mon->geometry.headroom_y;
            if (max_headroom <= 0.0f) max_headroom = (float)TE_HOVER_HEADROOM_BASE_PX;
            if (mon->anim[i].currentOffsetY < -max_headroom) {
                mon->anim[i].currentOffsetY = -max_headroom;
                mon->anim[i].velocityOffsetY = 0.0f;
            }
            if (mon->anim[i].currentOffsetY > 0.0f) {
                mon->anim[i].currentOffsetY = 0.0f;
                mon->anim[i].velocityOffsetY = 0.0f;
            }
            float push_limit = (config->drop_zone_push > 0.0f) ? config->drop_zone_push : 50.0f;
            if (mon->anim[i].current_pos_x > push_limit) mon->anim[i].current_pos_x = push_limit;
            if (mon->anim[i].current_pos_x < -push_limit) mon->anim[i].current_pos_x = -push_limit;
        }
    }

    /* Compute target scales */
    float target_scales[TE_HOVER_MAX_ICONS];
    float icon_centers[TE_HOVER_MAX_ICONS];

    for (int i = 0; i < icon_count; i++) {
        icon_centers[i] = mon->anim[i].center_x;
    }

    int drag_active = (mon->mouse.is_dragging && config->drag_drop_enabled) ? 1 : 0;
    float recession = (config->drag_recession_scale >= 0.1f && config->drag_recession_scale <= 1.0f)
                      ? config->drag_recession_scale : 0.80f;

    if (mon->mouse.is_in_taskbar) {
        /* Mouse is in taskbar: compute magnification wave */
        TE_MagnifyComputeScales(
            mon->mouse.cursor_x,
            icon_centers,
            target_scales,
            icon_count,
            (float)config->radius,
            config->max_scale,
            config->curve
        );

        /* Scale down hovered icon slightly during a drag if drag_drop_enabled */
        if (drag_active) {
            int closest_idx = -1;
            float min_dist = 1e9f;
            for (int i = 0; i < icon_count; i++) {
                float d = fabsf(icon_centers[i] - mon->mouse.cursor_x);
                if (d < min_dist) {
                    min_dist = d;
                    closest_idx = i;
                }
            }
            if (closest_idx >= 0 && min_dist < (float)config->radius) {
                target_scales[closest_idx] = recession;
            }
        }
    } else {
        /* Mouse left: settle targets are all 1.0 */
        for (int i = 0; i < icon_count; i++) {
            target_scales[i] = 1.0f;
        }
    }

    /* Smooth interpolation: lerp current scales toward targets */
    float base_speed = (config->speed_ms > 0)
                     ? (1000.0f / (float)config->speed_ms) * dt * 4.0f
                     : 1.0f;
    if (base_speed > 1.0f) base_speed = 1.0f;

    float in_speed = base_speed * 1.5f;
    if (in_speed > 1.0f) in_speed = 1.0f;

    float out_speed = base_speed * 1.2f;
    if (out_speed > 1.0f) out_speed = 1.0f;

    float min_step = 2.4f * dt;
    float max_diff = 0.0f;

    for (int i = 0; i < icon_count; i++) {
        float target = target_scales[i];
        float current = mon->anim[i].current_scale;
        float speed = mon->mouse.is_in_taskbar ? in_speed : out_speed;
        float delta = (target - current) * speed;

        float new_scale;
        if (fabsf(target - current) < 0.005f) {
            new_scale = target;
        } else {
            if (fabsf(delta) < min_step) {
                delta = (target > current) ? min_step : -min_step;
            }
            new_scale = current + delta;
            if ((current < target && new_scale > target) || (current > target && new_scale < target)) {
                new_scale = target;
            }
        }

        float min_scale = drag_active ? recession : 1.0f;
        if (new_scale < min_scale) new_scale = min_scale;
        if (new_scale > config->max_scale) new_scale = config->max_scale;

        mon->anim[i].current_scale = new_scale;
        mon->anim[i].target_scale = target;

        float diff = fabsf(new_scale - target);
        if (diff > max_diff) max_diff = diff;
    }

    /* Y-axis 2nd-order critically damped harmonic oscillator physics */
    const float k_spring_y = 200.0f;
    const float c_damping_y = 28.28f; /* 2 * sqrt(k_spring_y) */
    float max_headroom = (float)mon->geometry.headroom_y;
    if (max_headroom <= 0.0f) {
        max_headroom = (float)TE_HOVER_HEADROOM_BASE_PX;
    }

    bool all_y_settled = true;
    for (int i = 0; i < icon_count; i++) {
        float forceOffsetY = -k_spring_y * (mon->anim[i].currentOffsetY - mon->anim[i].targetOffsetY)
                             - c_damping_y * mon->anim[i].velocityOffsetY;
        mon->anim[i].velocityOffsetY += forceOffsetY * dt;
        mon->anim[i].currentOffsetY += mon->anim[i].velocityOffsetY * dt;

        /* Honor headroom constraint: prevent clipping beyond headroom_y */
        if (mon->anim[i].currentOffsetY < -max_headroom) {
            mon->anim[i].currentOffsetY = -max_headroom;
            if (mon->anim[i].velocityOffsetY < 0.0f) {
                mon->anim[i].velocityOffsetY = 0.0f;
            }
        }

        /* Prevent dipping below resting baseline floor */
        if (mon->anim[i].currentOffsetY > 0.0f) {
            mon->anim[i].currentOffsetY = 0.0f;
            mon->anim[i].velocityOffsetY = 0.0f;
        }

        /* Check settlement for Y */
        if (fabsf(mon->anim[i].currentOffsetY - mon->anim[i].targetOffsetY) > 0.5f ||
            fabsf(mon->anim[i].velocityOffsetY) > 1.0f) {
            all_y_settled = false;
        } else {
            mon->anim[i].currentOffsetY = mon->anim[i].targetOffsetY;
            mon->anim[i].velocityOffsetY = 0.0f;
        }
    }

    /* 3D Tilt target angle calculation */
    float max_tilt_deg = config->max_tilt_angle;
    if (max_tilt_deg < 0.0f) max_tilt_deg = 0.0f;
    if (max_tilt_deg > 45.0f) max_tilt_deg = 45.0f;
    const float deg2rad = 3.14159265358979323846f / 180.0f;
    const float MAX_TILT_RAD = config->tilt_enabled ? (max_tilt_deg * deg2rad) : 0.0f;
    float tilt_radius = (float)config->radius;
    if (tilt_radius <= 0.0f) tilt_radius = 120.0f;

    for (int i = 0; i < icon_count; i++) {
        if (mon->mouse.is_in_taskbar && config->tilt_enabled) {
            float dx = mon->mouse.cursor_x - mon->anim[i].center_x;
            float dy = mon->mouse.cursor_y - mon->anim[i].center_y;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist < tilt_radius) {
                float factor = 1.0f - (dist / tilt_radius);
                mon->anim[i].target_tilt_x = (dy / tilt_radius) * MAX_TILT_RAD * factor;
                mon->anim[i].target_tilt_y = (dx / tilt_radius) * MAX_TILT_RAD * factor;
            } else {
                mon->anim[i].target_tilt_x = 0.0f;
                mon->anim[i].target_tilt_y = 0.0f;
            }
        } else {
            mon->anim[i].target_tilt_x = 0.0f;
            mon->anim[i].target_tilt_y = 0.0f;
        }
    }

    /* 3D Tilt 2nd-order critically damped harmonic oscillator physics */
    const float k_spring_tilt = 250.0f;
    const float c_damping_tilt = 31.62f;

    bool all_tilt_settled = true;
    for (int i = 0; i < icon_count; i++) {
        float force_tilt_x = -k_spring_tilt * (mon->anim[i].current_tilt_x - mon->anim[i].target_tilt_x)
                             - c_damping_tilt * mon->anim[i].velocity_tilt_x;
        mon->anim[i].velocity_tilt_x += force_tilt_x * dt;
        mon->anim[i].current_tilt_x += mon->anim[i].velocity_tilt_x * dt;

        float force_tilt_y = -k_spring_tilt * (mon->anim[i].current_tilt_y - mon->anim[i].target_tilt_y)
                             - c_damping_tilt * mon->anim[i].velocity_tilt_y;
        mon->anim[i].velocity_tilt_y += force_tilt_y * dt;
        mon->anim[i].current_tilt_y += mon->anim[i].velocity_tilt_y * dt;

        if (fabsf(mon->anim[i].current_tilt_x - mon->anim[i].target_tilt_x) > 0.001f ||
            fabsf(mon->anim[i].velocity_tilt_x) > 0.01f ||
            fabsf(mon->anim[i].current_tilt_y - mon->anim[i].target_tilt_y) > 0.001f ||
            fabsf(mon->anim[i].velocity_tilt_y) > 0.01f) {
            all_tilt_settled = false;
        } else {
            mon->anim[i].current_tilt_x = mon->anim[i].target_tilt_x;
            mon->anim[i].velocity_tilt_x = 0.0f;
            mon->anim[i].current_tilt_y = mon->anim[i].target_tilt_y;
            mon->anim[i].velocity_tilt_y = 0.0f;
        }
    }

    /* Compute displaced positions */
    float target_pos_x[TE_HOVER_MAX_ICONS];
    float pos_y[TE_HOVER_MAX_ICONS];
    ComputeDisplacedPositions(mon->anim, icon_count, mon->mouse.cursor_x,
                              drag_active, mon->mouse.is_in_taskbar,
                              (float)config->radius,
                              config->drop_zone_push,
                              target_pos_x, pos_y);

    /* Smooth lerp of horizontal positions matching animation speed */
    float pos_lerp_speed = (config->speed_ms > 0)
                         ? (1000.0f / (float)config->speed_ms) * dt * 3.0f
                         : 15.0f * dt;
    if (pos_lerp_speed > 1.0f) pos_lerp_speed = 1.0f;

    float max_pos_diff = 0.0f;
    for (int i = 0; i < icon_count; i++) {
        float dist_target = target_pos_x[i] - mon->anim[i].current_pos_x;
        if (fabsf(dist_target) < 0.1f) {
            mon->anim[i].current_pos_x = target_pos_x[i];
        } else {
            mon->anim[i].current_pos_x += dist_target * pos_lerp_speed;
        }
        mon->soa_pos_x[i] = mon->anim[i].current_pos_x;
        mon->soa_pos_y[i] = mon->anim[i].currentOffsetY;
        mon->soa_scales[i] = mon->anim[i].current_scale;
        mon->soa_tilts_x[i] = mon->anim[i].current_tilt_x;
        mon->soa_tilts_y[i] = mon->anim[i].current_tilt_y;

        float pd = fabsf(mon->anim[i].current_pos_x - target_pos_x[i]);
        if (pd > max_pos_diff) max_pos_diff = pd;
    }

    /* Zero-repack Structure-of-Arrays (SoA) layout passed directly to DComp (PERF-303) */
    TE_DCompUpdateTransformsForTarget(mon->target_index, icon_count,
                                      mon->soa_scales, mon->soa_pos_x, mon->soa_pos_y,
                                      mon->soa_tilts_x, mon->soa_tilts_y);

    if (out_is_dirty) {
        bool is_dirty = false;
        for (int i = 0; i < icon_count; i++) {
            if (fabsf(mon->soa_scales[i] - old_scales[i]) > 0.0001f ||
                fabsf(mon->soa_pos_x[i] - old_pos_x[i]) > 0.0001f ||
                fabsf(mon->soa_pos_y[i] - old_pos_y[i]) > 0.0001f ||
                fabsf(mon->soa_tilts_x[i] - old_tilt_x[i]) > 0.0001f ||
                fabsf(mon->soa_tilts_y[i] - old_tilt_y[i]) > 0.0001f) {
                is_dirty = true;
                break;
            }
        }
        *out_is_dirty = is_dirty;
    }

    /* PERF-104: Stationary Hover Settlement Evaluation */
    bool is_settled = false;
    if (!mon->mouse.is_in_taskbar) {
        if (max_diff < 0.001f && max_pos_diff < 0.001f && all_y_settled && all_tilt_settled) {
            is_settled = true;
            TE_DCompSetOverlayAlphaForTarget(mon->target_index, 0.0f);

            mon->mouse.is_settling = 0;
            mon->mouse.settle_progress = 1.0f;

            for (int i = 0; i < icon_count; i++) {
                mon->anim[i].current_scale = 1.0f;
                mon->anim[i].target_scale = 1.0f;
                mon->anim[i].currentOffsetY = 0.0f;
                mon->anim[i].targetOffsetY = 0.0f;
                mon->anim[i].velocityOffsetY = 0.0f;
                mon->anim[i].current_tilt_x = 0.0f;
                mon->anim[i].target_tilt_x = 0.0f;
                mon->anim[i].velocity_tilt_x = 0.0f;
                mon->anim[i].current_tilt_y = 0.0f;
                mon->anim[i].target_tilt_y = 0.0f;
                mon->anim[i].velocity_tilt_y = 0.0f;
                mon->anim[i].current_pos_x = 0.0f;
            }
        } else {
            TE_DCompSetOverlayAlphaForTarget(mon->target_index, 1.0f);
        }
    } else {
        /* Cursor is stationary in taskbar and all dimensions have reached resting equilibrium */
        if (cursor_stationary && max_diff < 0.001f && max_pos_diff < 0.001f && all_y_settled && all_tilt_settled) {
            is_settled = true;
            /* Maintain visible overlay alpha during resting hover state */
            TE_DCompSetOverlayAlphaForTarget(mon->target_index, 1.0f);
        } else {
            TE_DCompSetOverlayAlphaForTarget(mon->target_index, 1.0f);
        }
    }

    return is_settled;
}

/**
 * Animation hot path tick:
 * Evaluates monitors and dynamic island, then commits DirectComposition.
 */
static VOID CALLBACK FrameTimerCallback(PVOID lpParam, BOOLEAN timer_or_wait_fired)
{
    (void)lpParam;
    (void)timer_or_wait_fired;

    if (!s_loop_active) return;
    TE_IconHoverState* state = &g_hover_state;
    if (!state->enabled) return;

    /* SYS-009: Explicitly initialize COM as MTA upon entry if uninitialized */
    struct ComMtaScopeGuard {
        HRESULT hr;
        ComMtaScopeGuard() { hr = CoInitializeEx(NULL, COINIT_MULTITHREADED); }
        ~ComMtaScopeGuard() { if (SUCCEEDED(hr)) CoUninitialize(); }
    } com_guard;

    struct CallbackThreadGuard {
        CallbackThreadGuard() { s_is_callback_thread = true; }
        ~CallbackThreadGuard() { s_is_callback_thread = false; }
    } guard;

    /* Calculate delta time */
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    float dt = 0.0f;
    if (s_last_tick_qpc > 0 && s_qpc_freq.QuadPart > 0) {
        dt = (float)((double)(now.QuadPart - (LONGLONG)s_last_tick_qpc) / (double)s_qpc_freq.QuadPart);
    }
    s_last_tick_qpc = (uint64_t)now.QuadPart;

    /* Clamp dt to reasonable range (avoid spikes after long pauses) */
    if (dt <= 0.0f) dt = 0.008f;
    if (dt > 0.05f) dt = 0.05f;

    /* PERF-205: Eliminate per-frame synchronous Z-order traversal */

    POINT cur = {};
    bool has_cur = (GetCursorPos(&cur) != FALSE);

    /* Track left-click transition for forwarding clicks on Dynamic Island and Start Button */
    static bool s_prev_lbutton_down = false;
    bool curr_lbutton_down = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    if (curr_lbutton_down && !s_prev_lbutton_down && has_cur) {
        bool click_handled = false;
        if (TE_DynamicIslandIsEnabled()) {
            click_handled = (TE_DynamicIslandCheckClick((float)cur.x, (float)cur.y) != FALSE);
        }
        if (!click_handled) {
            TE_FrameLoopCheckStartButtonClick((float)cur.x, (float)cur.y);
        }
    }
    s_prev_lbutton_down = curr_lbutton_down;

    bool all_monitors_settled = true;
    bool any_monitor_dirty = false;

    if (state->monitor_count > 0) {
        for (int m = 0; m < state->monitor_count; m++) {
            TE_MonitorState* mon = &state->monitors[m];
            if (!mon->is_active || mon->anim_count <= 0) continue;
            bool monitor_dirty = false;
            bool settled = ProcessMonitorTick(mon, m, &state->config, dt, (uint64_t)now.QuadPart, cur, has_cur, &monitor_dirty);
            if (monitor_dirty) {
                any_monitor_dirty = true;
            }
            if (!settled) {
                all_monitors_settled = false;
            }
        }
        /* Mirror monitor 0 to state->anim, state->mouse, state->anim_count for backwards compatibility */
        if (state->monitors[0].is_active) {
            state->anim_count = state->monitors[0].anim_count;
            state->mouse = state->monitors[0].mouse;
            memcpy(state->anim, state->monitors[0].anim, sizeof(TE_IconAnimState) * state->anim_count);
        }
    } else if (state->anim_count > 0) {
        /* Single monitor / legacy fallback */
        TE_MonitorState legacy_mon = {};
        legacy_mon.taskbar_hwnd = state->ctx ? state->ctx->taskbar_hwnd : NULL;
        legacy_mon.overlay_hwnd = state->overlay_hwnd;
        legacy_mon.geometry = state->geometry;
        legacy_mon.anim_count = state->anim_count;
        legacy_mon.mouse = state->mouse;
        legacy_mon.target_index = 0;
        legacy_mon.is_active = 1;
        memcpy(legacy_mon.anim, state->anim, sizeof(TE_IconAnimState) * state->anim_count);

        bool legacy_dirty = false;
        bool settled = ProcessMonitorTick(&legacy_mon, 0, &state->config, dt, (uint64_t)now.QuadPart, cur, has_cur, &legacy_dirty);
        if (legacy_dirty) any_monitor_dirty = true;
        if (!settled) {
            all_monitors_settled = false;
        }

        state->anim_count = legacy_mon.anim_count;
        memcpy(state->anim, legacy_mon.anim, sizeof(TE_IconAnimState) * state->anim_count);
        state->mouse = legacy_mon.mouse;
    }

    /* Process Dynamic Island Frame */
    bool island_settled = true;
    bool island_dirty = false;
    if (TE_DynamicIslandIsEnabled()) {
        TE_DynamicIslandUpdateFrame(dt);
        island_settled = (TE_DynamicIslandIsSettled() != FALSE);
        island_dirty = (TE_DynamicIslandIsDirty() != FALSE);
    }

    if (any_monitor_dirty || island_dirty) {
        /* Single composition commit presents all targets across all displays */
        TE_DCompCommit();
    }

    /* Check if settle animation is complete across all monitors and dynamic island */
    if (all_monitors_settled && island_settled) {
        if (InterlockedCompareExchange(&s_loop_active, 0, 1) == 1) {
            if (s_waitable_timer) {
                CancelWaitableTimer(s_waitable_timer);
            }
            s_last_tick_qpc = 0;
            TE_LogWrite(TE_LOG_DEBUG, LOG_TAG, "Settle complete for all displays, frame loop stopped automatically");
        }
    }
}

/**
 * Dedicated frame loop thread procedure (SYS-009, PERF-103).
 */
static DWORD WINAPI FrameLoopWorkerThread(LPVOID lpParam)
{
    (void)lpParam;

    /* Explicit COM MTA initialization for entire worker thread lifetime (SYS-009) */
    struct ComMtaScopeGuard {
        HRESULT hr;
        ComMtaScopeGuard() { hr = CoInitializeEx(NULL, COINIT_MULTITHREADED); }
        ~ComMtaScopeGuard() { if (SUCCEEDED(hr)) CoUninitialize(); }
    } com_guard;

    s_is_callback_thread = true;

    HANDLE idle_events[2] = { s_shutdown_event, s_wake_event };

    while (true) {
        DWORD res = WaitForMultipleObjects(2, idle_events, FALSE, INFINITE);
        if (res == WAIT_OBJECT_0) {
            break; // Shutdown signaled
        }

        ResetEvent(s_stop_event);

        double refresh_rate = TE_FrameLoopGetRefreshRate();
        if (refresh_rate < 24.0) refresh_rate = 60.0;
        LONGLONG interval_100ns = (LONGLONG)(10000000.0 / refresh_rate);
        if (interval_100ns < 20000) interval_100ns = 20000; // Cap at 500 Hz

        /* First tick fires immediately */
        LARGE_INTEGER due_time;
        due_time.QuadPart = -1;
        SetWaitableTimer(s_waitable_timer, &due_time, 0, NULL, NULL, FALSE);

        HANDLE active_events[3] = { s_shutdown_event, s_stop_event, s_waitable_timer };

        while (s_loop_active) {
            DWORD active_res = WaitForMultipleObjects(3, active_events, FALSE, INFINITE);
            if (active_res == WAIT_OBJECT_0) {
                CancelWaitableTimer(s_waitable_timer);
                goto thread_exit;
            }
            if (active_res == WAIT_OBJECT_0 + 1) {
                CancelWaitableTimer(s_waitable_timer);
                break;
            }
            if (active_res == WAIT_OBJECT_0 + 2) {
                if (!s_loop_active) {
                    CancelWaitableTimer(s_waitable_timer);
                    break;
                }

                FrameTimerCallback(NULL, TRUE);

                if (!s_loop_active) {
                    CancelWaitableTimer(s_waitable_timer);
                    break;
                }

                due_time.QuadPart = -interval_100ns;
                SetWaitableTimer(s_waitable_timer, &due_time, 0, NULL, NULL, FALSE);
            }
        }
    }

thread_exit:
    s_is_callback_thread = false;
    return 0;
}

static void EnsureWorkerInitialized(void)
{
    if (s_worker_initialized && s_thread != NULL) {
        return;
    }

    if (!s_wake_event) {
        s_wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    }
    if (!s_stop_event) {
        s_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    }
    if (!s_shutdown_event) {
        s_shutdown_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    }

    if (!s_waitable_timer) {
        s_waitable_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (s_waitable_timer) {
            s_is_high_res = 1;
        } else {
            s_waitable_timer = CreateWaitableTimerW(NULL, FALSE, NULL);
            s_is_high_res = 0;
        }
    }

    if (!s_thread) {
        s_thread = CreateThread(NULL, 0, FrameLoopWorkerThread, NULL, 0, NULL);
        if (s_thread) {
            SetThreadPriority(s_thread, THREAD_PRIORITY_HIGHEST);
        }
    }

    s_worker_initialized = (s_thread != NULL);
}

void TE_FrameLoopWakeDynamicIsland(void)
{
    TE_FrameLoopStart();
}

HRESULT TE_FrameLoopStart(void)
{
    /* Register dynamic island wake callback */
    TE_DynamicIslandSetWakeCallback(TE_FrameLoopWakeDynamicIsland);

    EnsureWorkerInitialized();

    /* Already running? No-op */
    if (InterlockedCompareExchange(&s_loop_active, 1, 0) == 1) {
        return TE_S_OK;
    }

    if (s_stop_event) {
        ResetEvent(s_stop_event);
    }

    /* Initialize QPC frequency */
    if (s_qpc_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&s_qpc_freq);
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    s_last_tick_qpc = (uint64_t)now.QuadPart;

    /* Show overlay */
    TE_DCompSetOverlayAlpha(1.0f);
    if (g_hover_state.monitor_count > 0) {
        for (int i = 0; i < g_hover_state.monitor_count; i++) {
            if (g_hover_state.monitors[i].is_active) {
                TE_DCompSetOverlayAlphaForTarget(g_hover_state.monitors[i].target_index, 1.0f);
            }
        }
    }
    if (g_hover_state.config.keep_on_top) {
        TE_DCompEnsureTopmost(NULL);
    }
    TE_DCompCommit();

    if (s_wake_event) {
        SetEvent(s_wake_event);
    }

    TE_LogWrite(TE_LOG_DEBUG, LOG_TAG, "Frame loop started");
    return TE_S_OK;
}

void TE_FrameLoopStop(void)
{
    if (InterlockedCompareExchange(&s_loop_active, 0, 1) == 0) {
        return; /* Already stopped */
    }

    if (s_stop_event) {
        SetEvent(s_stop_event);
    }
    if (s_waitable_timer) {
        CancelWaitableTimer(s_waitable_timer);
    }

    s_last_tick_qpc = 0;

    TE_LogWrite(TE_LOG_DEBUG, LOG_TAG, "Frame loop stopped");
}

void TE_FrameLoopShutdown(void)
{
    TE_FrameLoopStop();
    if (s_shutdown_event) {
        SetEvent(s_shutdown_event);
    }
    if (s_thread) {
        WaitForSingleObject(s_thread, 1000);
        CloseHandle(s_thread);
        s_thread = NULL;
    }
    if (s_waitable_timer) {
        CloseHandle(s_waitable_timer);
        s_waitable_timer = NULL;
    }
    if (s_wake_event) {
        CloseHandle(s_wake_event);
        s_wake_event = NULL;
    }
    if (s_stop_event) {
        CloseHandle(s_stop_event);
        s_stop_event = NULL;
    }
    if (s_shutdown_event) {
        CloseHandle(s_shutdown_event);
        s_shutdown_event = NULL;
    }
    s_worker_initialized = false;
    for (int m = 0; m < TE_MAX_MONITORS; m++) {
        s_has_prev_cursor[m] = false;
    }
}

int TE_FrameLoopIsActive(void)
{
    return InterlockedCompareExchange(&s_loop_active, 0, 0);
}

void TE_FrameLoopOnMouseMoveEx(float cursor_x, float cursor_y, int is_dragging, HWND taskbar_hwnd)
{
    TE_IconHoverState* state = &g_hover_state;
    AcquireSRWLockExclusive(&state->state_lock);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t now_qpc = (uint64_t)now.QuadPart;

    int active_idx = -1;
    if (state->monitor_count > 0) {
        if (taskbar_hwnd) {
            for (int i = 0; i < state->monitor_count; i++) {
                if (state->monitors[i].taskbar_hwnd == taskbar_hwnd) {
                    active_idx = i;
                    break;
                }
            }
        }
        if (active_idx == -1) {
            /* Hit test cursor against taskbar rects (including upward headroom) */
            POINT pt = { (LONG)cursor_x, (LONG)cursor_y };
            for (int i = 0; i < state->monitor_count; i++) {
                if (!state->monitors[i].is_active) continue;
                RECT r = state->monitors[i].geometry.taskbarRect;
                r.top -= state->monitors[i].geometry.headroom_y;
                if (PtInRect(&r, pt)) {
                    active_idx = i;
                    break;
                }
            }
        }
        if (active_idx == -1) {
            active_idx = 0;
        }

        /* Update active monitor, transition other monitors to settle */
        for (int i = 0; i < state->monitor_count; i++) {
            if (i == active_idx) {
                state->monitors[i].mouse.cursor_x = cursor_x;
                state->monitors[i].mouse.cursor_y = cursor_y;
                state->monitors[i].mouse.is_in_taskbar = 1;
                state->monitors[i].mouse.is_dragging = is_dragging;
                state->monitors[i].mouse.is_settling = 0;
                state->monitors[i].mouse.last_mousemove_qpc = now_qpc;
            } else if (state->monitors[i].mouse.is_in_taskbar) {
                state->monitors[i].mouse.is_in_taskbar = 0;
                state->monitors[i].mouse.is_dragging = 0;
                state->monitors[i].mouse.is_settling = 1;
                state->monitors[i].mouse.settle_progress = 0.0f;
            }

            /* PERF-101: Atomically swap double-buffered snapshot */
            int cur_b = s_monitor_buffers[i].active_idx.load(std::memory_order_relaxed);
            int next_b = 1 - cur_b;
            s_monitor_buffers[i].mouse_snapshots[next_b].cursor_x = state->monitors[i].mouse.cursor_x;
            s_monitor_buffers[i].mouse_snapshots[next_b].cursor_y = state->monitors[i].mouse.cursor_y;
            s_monitor_buffers[i].mouse_snapshots[next_b].is_in_taskbar = state->monitors[i].mouse.is_in_taskbar;
            s_monitor_buffers[i].mouse_snapshots[next_b].is_dragging = state->monitors[i].mouse.is_dragging;
            s_monitor_buffers[i].mouse_snapshots[next_b].is_settling = state->monitors[i].mouse.is_settling;
            s_monitor_buffers[i].mouse_snapshots[next_b].settle_progress = state->monitors[i].mouse.settle_progress;
            s_monitor_buffers[i].mouse_snapshots[next_b].last_mousemove_qpc = state->monitors[i].mouse.last_mousemove_qpc;
            s_monitor_buffers[i].active_idx.store(next_b, std::memory_order_release);
        }
    }

    /* Update global/legacy state->mouse for backward compatibility */
    if (active_idx <= 0) {
        state->mouse.cursor_x = cursor_x;
        state->mouse.cursor_y = cursor_y;
        state->mouse.is_in_taskbar = 1;
        state->mouse.is_dragging = is_dragging;
        state->mouse.is_settling = 0;
        state->mouse.last_mousemove_qpc = now_qpc;
    } else {
        if (state->mouse.is_in_taskbar) {
            state->mouse.is_in_taskbar = 0;
            state->mouse.is_dragging = 0;
            state->mouse.is_settling = 1;
            state->mouse.settle_progress = 0.0f;
        }
    }

    if (state->monitor_count == 0) {
        int cur_b = s_monitor_buffers[0].active_idx.load(std::memory_order_relaxed);
        int next_b = 1 - cur_b;
        s_monitor_buffers[0].mouse_snapshots[next_b].cursor_x = state->mouse.cursor_x;
        s_monitor_buffers[0].mouse_snapshots[next_b].cursor_y = state->mouse.cursor_y;
        s_monitor_buffers[0].mouse_snapshots[next_b].is_in_taskbar = state->mouse.is_in_taskbar;
        s_monitor_buffers[0].mouse_snapshots[next_b].is_dragging = state->mouse.is_dragging;
        s_monitor_buffers[0].mouse_snapshots[next_b].is_settling = state->mouse.is_settling;
        s_monitor_buffers[0].mouse_snapshots[next_b].settle_progress = state->mouse.settle_progress;
        s_monitor_buffers[0].mouse_snapshots[next_b].last_mousemove_qpc = state->mouse.last_mousemove_qpc;
        s_monitor_buffers[0].active_idx.store(next_b, std::memory_order_release);
    }

    ReleaseSRWLockExclusive(&state->state_lock);

    if (TE_DynamicIslandIsEnabled()) {
        TE_DynamicIslandOnMouseMove(cursor_x, cursor_y);
    }

    /* Start frame loop if not already running */
    if (!TE_FrameLoopIsActive()) {
        TE_FrameLoopStart();
    }
}

void TE_FrameLoopOnMouseMove(float cursor_x, float cursor_y, int is_dragging)
{
    TE_FrameLoopOnMouseMoveEx(cursor_x, cursor_y, is_dragging, NULL);
}

void TE_FrameLoopOnMouseLeave(void)
{
    TE_IconHoverState* state = &g_hover_state;
    AcquireSRWLockExclusive(&state->state_lock);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t now_qpc = (uint64_t)now.QuadPart;

    if (state->monitor_count > 0) {
        for (int i = 0; i < state->monitor_count; i++) {
            if (state->monitors[i].mouse.is_in_taskbar) {
                state->monitors[i].mouse.is_in_taskbar = 0;
                state->monitors[i].mouse.is_dragging = 0;
                state->monitors[i].mouse.is_settling = 1;
                state->monitors[i].mouse.settle_progress = 0.0f;
                state->monitors[i].mouse.last_mousemove_qpc = now_qpc;
            }

            /* PERF-101: Atomically swap double-buffered snapshot */
            int cur_b = s_monitor_buffers[i].active_idx.load(std::memory_order_relaxed);
            int next_b = 1 - cur_b;
            s_monitor_buffers[i].mouse_snapshots[next_b].cursor_x = state->monitors[i].mouse.cursor_x;
            s_monitor_buffers[i].mouse_snapshots[next_b].cursor_y = state->monitors[i].mouse.cursor_y;
            s_monitor_buffers[i].mouse_snapshots[next_b].is_in_taskbar = 0;
            s_monitor_buffers[i].mouse_snapshots[next_b].is_dragging = 0;
            s_monitor_buffers[i].mouse_snapshots[next_b].is_settling = 1;
            s_monitor_buffers[i].mouse_snapshots[next_b].settle_progress = 0.0f;
            s_monitor_buffers[i].mouse_snapshots[next_b].last_mousemove_qpc = now_qpc;
            s_monitor_buffers[i].active_idx.store(next_b, std::memory_order_release);
        }
    }

    state->mouse.is_in_taskbar = 0;
    state->mouse.is_dragging = 0;
    state->mouse.is_settling = 1;
    state->mouse.settle_progress = 0.0f;
    state->mouse.last_mousemove_qpc = now_qpc;

    if (state->monitor_count == 0) {
        int cur_b = s_monitor_buffers[0].active_idx.load(std::memory_order_relaxed);
        int next_b = 1 - cur_b;
        s_monitor_buffers[0].mouse_snapshots[next_b].cursor_x = state->mouse.cursor_x;
        s_monitor_buffers[0].mouse_snapshots[next_b].cursor_y = state->mouse.cursor_y;
        s_monitor_buffers[0].mouse_snapshots[next_b].is_in_taskbar = 0;
        s_monitor_buffers[0].mouse_snapshots[next_b].is_dragging = 0;
        s_monitor_buffers[0].mouse_snapshots[next_b].is_settling = 1;
        s_monitor_buffers[0].mouse_snapshots[next_b].settle_progress = 0.0f;
        s_monitor_buffers[0].mouse_snapshots[next_b].last_mousemove_qpc = now_qpc;
        s_monitor_buffers[0].active_idx.store(next_b, std::memory_order_release);
    }

    ReleaseSRWLockExclusive(&state->state_lock);

    if (TE_DynamicIslandIsEnabled()) {
        TE_DynamicIslandOnMouseLeave();
    }

    /* Start frame loop if not already running to animate return to baseline */
    if (!TE_FrameLoopIsActive()) {
        TE_FrameLoopStart();
    }
}

HRESULT TE_FrameLoopTriggerIconBounceForMonitor(int monitor_index, int icon_index, float impulse_strength)
{
    TE_IconHoverState* state = &g_hover_state;
    if (!state->enabled) return TE_E_FAIL;

    AcquireSRWLockExclusive(&state->state_lock);

    if (state->monitor_count > 0) {
        if (monitor_index < 0 || monitor_index >= state->monitor_count) {
            ReleaseSRWLockExclusive(&state->state_lock);
            return TE_E_INVALIDARG;
        }
        TE_MonitorState* mon = &state->monitors[monitor_index];
        if (icon_index < 0 || icon_index >= mon->anim_count) {
            ReleaseSRWLockExclusive(&state->state_lock);
            return TE_E_INVALIDARG;
        }

        mon->anim[icon_index].velocityOffsetY = -fabsf(impulse_strength);
        mon->anim[icon_index].targetOffsetY = 0.0f;

        if (monitor_index == 0 && icon_index < state->anim_count) {
            state->anim[icon_index].velocityOffsetY = -fabsf(impulse_strength);
            state->anim[icon_index].targetOffsetY = 0.0f;
        }
    } else {
        if (icon_index < 0 || icon_index >= state->anim_count) {
            ReleaseSRWLockExclusive(&state->state_lock);
            return TE_E_INVALIDARG;
        }
        state->anim[icon_index].velocityOffsetY = -fabsf(impulse_strength);
        state->anim[icon_index].targetOffsetY = 0.0f;
    }

    /* PERF-101: Lockless atomic impulse registration */
    int m_idx = (state->monitor_count > 0) ? monitor_index : 0;
    if (m_idx >= 0 && m_idx < TE_MAX_MONITORS && icon_index >= 0 && icon_index < TE_HOVER_MAX_ICONS) {
        s_monitor_buffers[m_idx].pending_impulses[icon_index].store(fabsf(impulse_strength), std::memory_order_release);
    }

    ReleaseSRWLockExclusive(&state->state_lock);

    /* Ensure frame loop is running and overlay is visible */
    if (!TE_FrameLoopIsActive()) {
        TE_FrameLoopStart();
    } else {
        int target_idx = (state->monitor_count > 0 && monitor_index < state->monitor_count)
                       ? state->monitors[monitor_index].target_index : 0;
        TE_DCompSetOverlayAlphaForTarget(target_idx, 1.0f);
        TE_DCompCommit();
    }

    return TE_S_OK;
}

HRESULT TE_FrameLoopTriggerIconBounce(int icon_index, float impulse_strength)
{
    return TE_FrameLoopTriggerIconBounceForMonitor(0, icon_index, impulse_strength);
}

void TE_TriggerStartMenu(void)
{
    /* Method 1: Synthesize Windows key press */
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LWIN;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_LWIN;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));

    /* Method 2: Also post SC_TASKLIST to Shell_TrayWnd */
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (taskbar) {
        PostMessageW(taskbar, WM_SYSCOMMAND, SC_TASKLIST, 0);
    }

    TE_LogWrite(TE_LOG_INFO, LOG_TAG, "Triggered Start Menu open");
}

int TE_FrameLoopCheckStartButtonClick(float cursor_x, float cursor_y)
{
    TE_IconHoverState* state = &g_hover_state;
    if (!state->enabled) return 0;

    POINT pt = { (LONG)cursor_x, (LONG)cursor_y };

    if (state->monitor_count > 0) {
        for (int m = 0; m < state->monitor_count; m++) {
            TE_MonitorState* mon = &state->monitors[m];
            if (!mon->is_active) continue;
            for (int i = 0; i < mon->anim_count; i++) {
                if (mon->icon_cache.items[i].element_type == TE_ELEM_START_BUTTON) {
                    float cur_scale = mon->anim[i].current_scale;
                    float cur_w = mon->anim[i].base_width * cur_scale;
                    float cur_h = mon->anim[i].base_height * cur_scale;
                    float cur_cx = mon->anim[i].center_x + mon->anim[i].current_pos_x;
                    float cur_cy = mon->anim[i].center_y + mon->anim[i].currentOffsetY;

                    RECT start_rect;
                    start_rect.left = (LONG)(cur_cx - cur_w / 2.0f);
                    start_rect.right = (LONG)(cur_cx + cur_w / 2.0f);
                    start_rect.top = (LONG)(cur_cy - cur_h / 2.0f);
                    start_rect.bottom = (LONG)(cur_cy + cur_h / 2.0f);

                    if (PtInRect(&start_rect, pt)) {
                        TE_TriggerStartMenu();
                        return 1;
                    }
                    break;
                }
            }
        }
    }

    /* Fallback for single monitor / legacy state */
    int icon_count = state->anim_count;
    for (int i = 0; i < icon_count; i++) {
        if (state->icon_cache.items[i].element_type == TE_ELEM_START_BUTTON) {
            float cur_scale = state->anim[i].current_scale;
            float cur_w = state->anim[i].base_width * cur_scale;
            float cur_h = state->anim[i].base_height * cur_scale;
            float cur_cx = state->anim[i].center_x + state->anim[i].current_pos_x;
            float cur_cy = state->anim[i].center_y + state->anim[i].currentOffsetY;

            RECT start_rect;
            start_rect.left = (LONG)(cur_cx - cur_w / 2.0f);
            start_rect.right = (LONG)(cur_cx + cur_w / 2.0f);
            start_rect.top = (LONG)(cur_cy - cur_h / 2.0f);
            start_rect.bottom = (LONG)(cur_cy + cur_h / 2.0f);

            if (PtInRect(&start_rect, pt)) {
                TE_TriggerStartMenu();
                return 1;
            }
            break;
        }
    }
    return 0;
}
