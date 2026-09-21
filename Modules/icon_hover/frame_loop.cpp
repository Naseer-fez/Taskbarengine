/**
 * @file frame_loop.cpp
 * @brief Vsync-aligned animation timer and DComp commit loop for IconHover.
 *
 * Uses CreateTimerQueueTimer at ~8ms (125 Hz) to drive the magnification
 * animation. On each tick:
 *   1. Read cursor position from shared hover state
 *   2. Compute TE_MagnifyComputeScales() for all icons
 *   3. Lerp current scales toward target (smooth interpolation)
 *   4. Compute displaced icon positions (push neighbors outward)
 *   5. Call TE_DCompUpdateTransforms() + TE_DCompCommit()
 *
 * Self-canceling: when mouse has left and max(|scale - 1.0|) < 0.001,
 * the timer deletes itself. Safety fallback: cancel if no WM_MOUSEMOVE
 * received for 500ms.
 */

#include "frame_loop.h"
#include "icon_hover_internal.h"
#include "magnification.h"
#include "dcomp_overlay.h"
#include "dynamic_island.h"
#include <sdk/te_log.h>

#include <windows.h>
#include <math.h>

/** Frame loop timer interval in milliseconds (~125 Hz). */
#define FRAME_TIMER_INTERVAL_MS 8

/** Safety timeout: cancel if no mouse move received for this many ms. */
#define MOUSE_TIMEOUT_MS 500

static const char* LOG_TAG = "FrameLoop";

/** Timer queue and timer handle. */
static HANDLE s_timer_queue = NULL;
static HANDLE s_timer_handle = NULL;
static volatile LONG s_loop_active = 0;

/** QPC frequency for delta time calculation. */
static LARGE_INTEGER s_qpc_freq = {};
static uint64_t s_last_tick_qpc = 0;



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
    const TE_HoverConfig* config,
    float dt,
    uint64_t now_qpc,
    POINT cur,
    bool has_cur,
    bool* out_is_dirty)
{
    int icon_count = mon->anim_count;
    if (icon_count <= 0) {
        if (out_is_dirty) *out_is_dirty = false;
        return true;
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


    /* Active cursor position validation: check if cursor is in active taskbar rect */
    RECT active_rect = mon->geometry.taskbarRect;
    active_rect.top -= mon->geometry.headroom_y;

    if (mon->mouse.is_in_taskbar && has_cur) {
        if (active_rect.right > active_rect.left && !PtInRect(&active_rect, cur)) {
            /* Cursor left the taskbar region: begin smooth settle */
            mon->mouse.is_in_taskbar = 0;
            mon->mouse.is_settling = 1;
            mon->mouse.settle_progress = 0.0f;
        } else {
            /* Cursor is still in taskbar: update current cursor position */
            mon->mouse.cursor_x = (float)cur.x;
            mon->mouse.cursor_y = (float)cur.y;
            mon->mouse.last_mousemove_qpc = now_qpc;
            if (mon->mouse.is_dragging && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                mon->mouse.is_dragging = 0;
            }
        }
    }

    /* Validate geometry generation */
    int generation_mismatch = 0;
    for (int i = 0; i < icon_count; i++) {
        if (mon->anim[i].geometry_generation != mon->geometry.generation) {
            generation_mismatch = 1;
            break;
        }
    }
    if (generation_mismatch) {
        /* Drop frame during resize transition to prevent floating artifacts */
        return false;
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

        if (fabsf(target - current) > 0.0001f) {
            if (fabsf(delta) < min_step) {
                delta = (target > current) ? min_step : -min_step;
            }
        }

        float new_scale = current + delta;
        if ((current < target && new_scale > target) || (current > target && new_scale < target)) {
            new_scale = target;
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

    /* Smooth lerp of horizontal positions */
    float pos_lerp_speed = 15.0f * dt;
    if (pos_lerp_speed > 1.0f) pos_lerp_speed = 1.0f;

    float pos_x[TE_HOVER_MAX_ICONS];
    for (int i = 0; i < icon_count; i++) {
        mon->anim[i].current_pos_x += (target_pos_x[i] - mon->anim[i].current_pos_x) * pos_lerp_speed;
        pos_x[i] = mon->anim[i].current_pos_x;
    }

    /* Update DComp transforms for target including 3D tilts */
    float scales[TE_HOVER_MAX_ICONS];
    float tilts_x[TE_HOVER_MAX_ICONS];
    float tilts_y[TE_HOVER_MAX_ICONS];
    for (int i = 0; i < icon_count; i++) {
        scales[i] = mon->anim[i].current_scale;
        tilts_x[i] = mon->anim[i].current_tilt_x;
        tilts_y[i] = mon->anim[i].current_tilt_y;
    }

    TE_DCompUpdateTransformsForTarget(mon->target_index, icon_count, scales, pos_x, pos_y, tilts_x, tilts_y);

    if (out_is_dirty) {
        bool is_dirty = false;
        for (int i = 0; i < icon_count; i++) {
            if (fabsf(scales[i] - old_scales[i]) > 0.0001f ||
                fabsf(pos_x[i] - old_pos_x[i]) > 0.0001f ||
                fabsf(pos_y[i] - old_pos_y[i]) > 0.0001f ||
                fabsf(tilts_x[i] - old_tilt_x[i]) > 0.0001f ||
                fabsf(tilts_y[i] - old_tilt_y[i]) > 0.0001f) {
                is_dirty = true;
                break;
            }
        }
        *out_is_dirty = is_dirty;
    }

    /* Check settlement */
    bool is_settled = (!mon->mouse.is_in_taskbar && max_diff < 0.001f && all_y_settled && all_tilt_settled);
    if (is_settled) {
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

    return is_settled;
}

/**
 * Timer callback — the animation hot path.
 * Called on a thread pool thread by the timer queue.
 */
static VOID CALLBACK FrameTimerCallback(PVOID lpParam, BOOLEAN timer_or_wait_fired)
{
    (void)lpParam;
    (void)timer_or_wait_fired;

    if (!s_loop_active) return;
    TE_IconHoverState* state = &g_hover_state;
    if (!state->enabled) return;

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

    /* Ensure overlays stay on top of taskbars even if user clicked taskbar */
    if (state->config.keep_on_top) {
        TE_DCompEnsureTopmost(NULL);
    }

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
            bool settled = ProcessMonitorTick(mon, &state->config, dt, (uint64_t)now.QuadPart, cur, has_cur, &monitor_dirty);
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
        bool settled = ProcessMonitorTick(&legacy_mon, &state->config, dt, (uint64_t)now.QuadPart, cur, has_cur, &legacy_dirty);
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
            if (s_timer_handle) {
                DeleteTimerQueueTimer(s_timer_queue, s_timer_handle, NULL);
                s_timer_handle = NULL;
            }
            s_last_tick_qpc = 0;
            TE_LogWrite(TE_LOG_DEBUG, LOG_TAG, "Settle complete for all displays, frame loop stopped automatically");
        }
    }
}

void TE_FrameLoopWakeDynamicIsland(void)
{
    TE_FrameLoopStart();
}

HRESULT TE_FrameLoopStart(void)
{
    /* Register dynamic island wake callback */
    TE_DynamicIslandSetWakeCallback(TE_FrameLoopWakeDynamicIsland);

    /* Already running? No-op */
    if (InterlockedCompareExchange(&s_loop_active, 1, 0) == 1) {
        return TE_S_OK;
    }

    /* Initialize QPC frequency */
    if (s_qpc_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&s_qpc_freq);
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    s_last_tick_qpc = (uint64_t)now.QuadPart;

    /* Create timer queue if needed */
    if (!s_timer_queue) {
        s_timer_queue = CreateTimerQueue();
        if (!s_timer_queue) {
            TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create timer queue");
            InterlockedExchange(&s_loop_active, 0);
            return TE_E_FAIL;
        }
    }

    /* Create periodic timer */
    BOOL ok = CreateTimerQueueTimer(
        &s_timer_handle,
        s_timer_queue,
        FrameTimerCallback,
        NULL,
        0,                        /* Due time: fire immediately */
        FRAME_TIMER_INTERVAL_MS,  /* Period: ~8ms (125 Hz) */
        WT_EXECUTEDEFAULT
    );

    if (!ok) {
        TE_LogWrite(TE_LOG_ERROR, LOG_TAG, "Failed to create timer queue timer");
        InterlockedExchange(&s_loop_active, 0);
        return TE_E_FAIL;
    }

    /* Show overlay */
    TE_DCompSetOverlayAlpha(1.0f);
    if (g_hover_state.monitor_count > 0) {
        for (int i = 0; i < g_hover_state.monitor_count; i++) {
            if (g_hover_state.monitors[i].is_active) {
                TE_DCompSetOverlayAlphaForTarget(g_hover_state.monitors[i].target_index, 1.0f);
            }
        }
    }
    TE_DCompCommit();

    TE_LogWrite(TE_LOG_DEBUG, LOG_TAG, "Frame loop started");
    return TE_S_OK;
}

void TE_FrameLoopStop(void)
{
    if (InterlockedCompareExchange(&s_loop_active, 0, 1) == 0) {
        return; /* Already stopped */
    }

    if (s_timer_handle) {
        /* Use NULL to cancel asynchronously and prevent thread-pool self-deadlocks */
        DeleteTimerQueueTimer(s_timer_queue, s_timer_handle, NULL);
        s_timer_handle = NULL;
    }

    s_last_tick_qpc = 0;

    TE_LogWrite(TE_LOG_DEBUG, LOG_TAG, "Frame loop stopped");
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

    ReleaseSRWLockExclusive(&state->state_lock);

    if (TE_DynamicIslandIsEnabled()) {
        TE_DynamicIslandOnMouseMove(cursor_x, cursor_y);
    }

    /* Start frame loop if not already running, or re-assert topmost if running */
    if (!TE_FrameLoopIsActive()) {
        TE_FrameLoopStart();
    } else if (state->config.keep_on_top) {
        TE_DCompEnsureTopmost(taskbar_hwnd);
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

    if (state->monitor_count > 0) {
        for (int i = 0; i < state->monitor_count; i++) {
            if (state->monitors[i].mouse.is_in_taskbar) {
                state->monitors[i].mouse.is_in_taskbar = 0;
                state->monitors[i].mouse.is_dragging = 0;
                state->monitors[i].mouse.is_settling = 1;
                state->monitors[i].mouse.settle_progress = 0.0f;
            }
        }
    }

    state->mouse.is_in_taskbar = 0;
    state->mouse.is_dragging = 0;
    state->mouse.is_settling = 1;
    state->mouse.settle_progress = 0.0f;

    ReleaseSRWLockExclusive(&state->state_lock);

    if (TE_DynamicIslandIsEnabled()) {
        TE_DynamicIslandOnMouseLeave();
    }

    /* Frame loop continues running to animate the settle */
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
