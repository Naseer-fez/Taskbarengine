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
 * Timer callback — the animation hot path.
 * Called on a thread pool thread by the timer queue.
 */
static VOID CALLBACK FrameTimerCallback(PVOID lpParam, BOOLEAN timer_or_wait_fired)
{
    (void)lpParam;
    (void)timer_or_wait_fired;

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

    int icon_count = state->anim_count;
    if (icon_count <= 0) return;

    /* Ensure overlay stays on top of taskbar even if user clicked taskbar */
    if (state->config.keep_on_top) {
        HWND taskbar_hwnd = state->ctx ? state->ctx->taskbar_hwnd : NULL;
        TE_DCompEnsureTopmost(taskbar_hwnd);
    }

    /* Active cursor position validation: check if cursor is in active taskbar rect (including upward headroom) */
    RECT active_rect = state->geometry.taskbarRect;
    active_rect.top -= state->geometry.headroom_y;

    if (state->mouse.is_in_taskbar) {
        POINT cur;
        if (GetCursorPos(&cur)) {
            if (active_rect.right > active_rect.left && !PtInRect(&active_rect, cur)) {
                /* Cursor left the taskbar region: begin smooth settle */
                state->mouse.is_in_taskbar = 0;
                state->mouse.is_settling = 1;
                state->mouse.settle_progress = 0.0f;
            } else {
                /* Cursor is still in taskbar: update current cursor position */
                state->mouse.cursor_x = (float)cur.x;
                state->mouse.cursor_y = (float)cur.y;
                state->mouse.last_mousemove_qpc = (uint64_t)now.QuadPart;
                if (state->mouse.is_dragging && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    state->mouse.is_dragging = 0;
                }
            }
        }
    }

    /* Track left-click transition for forwarding clicks on Start Button to open Start Menu */
    static bool s_prev_lbutton_down = false;
    bool curr_lbutton_down = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    if (curr_lbutton_down && !s_prev_lbutton_down) {
        TE_FrameLoopCheckStartButtonClick(state->mouse.cursor_x, state->mouse.cursor_y);
    }
    s_prev_lbutton_down = curr_lbutton_down;

    /* Compute target scales */
    float target_scales[TE_HOVER_MAX_ICONS];
    float icon_centers[TE_HOVER_MAX_ICONS];

    for (int i = 0; i < icon_count; i++) {
        icon_centers[i] = state->anim[i].center_x;
    }

    int drag_active = (state->mouse.is_dragging && state->config.drag_drop_enabled) ? 1 : 0;
    float recession = (state->config.drag_recession_scale >= 0.1f && state->config.drag_recession_scale <= 1.0f)
                      ? state->config.drag_recession_scale : 0.80f;

    if (state->mouse.is_in_taskbar) {
        /* Mouse is in taskbar: compute magnification wave */
        TE_MagnifyComputeScales(
            state->mouse.cursor_x,
            icon_centers,
            target_scales,
            icon_count,
            (float)state->config.radius,
            state->config.max_scale,
            state->config.curve
        );

        /* Phase 2: Scale down hovered icon slightly during a drag if drag_drop_enabled */
        if (drag_active) {
            int closest_idx = -1;
            float min_dist = 1e9f;
            for (int i = 0; i < icon_count; i++) {
                float d = fabsf(icon_centers[i] - state->mouse.cursor_x);
                if (d < min_dist) {
                    min_dist = d;
                    closest_idx = i;
                }
            }
            if (closest_idx >= 0 && min_dist < (float)state->config.radius) {
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
    float base_speed = (state->config.speed_ms > 0)
                     ? (1000.0f / (float)state->config.speed_ms) * dt * 4.0f
                     : 1.0f;
    if (base_speed > 1.0f) base_speed = 1.0f;

    /* For mouse-in, use faster interpolation for responsive feel */
    float in_speed = base_speed * 1.5f;
    if (in_speed > 1.0f) in_speed = 1.0f;

    /* User requested 1.2x speed for the drop/fallout */
    float out_speed = base_speed * 1.2f;
    if (out_speed > 1.0f) out_speed = 1.0f;

    /* Minimum step to ensure the animation completes cleanly and quickly */
    float min_step = 2.4f * dt;

    float max_diff = 0.0f;
    for (int i = 0; i < icon_count; i++) {
        float target = target_scales[i];
        float current = state->anim[i].current_scale;

        /* Use faster lerp when approaching target (mouse in), use out_speed for settle */
        float speed = state->mouse.is_in_taskbar ? in_speed : out_speed;
        
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

        /* Clamp to valid range (allow recession during drag if enabled) */
        float min_scale = drag_active ? recession : 1.0f;
        if (new_scale < min_scale) new_scale = min_scale;
        if (new_scale > state->config.max_scale) new_scale = state->config.max_scale;

        state->anim[i].current_scale = new_scale;
        state->anim[i].target_scale = target;

        float diff = fabsf(new_scale - target);
        if (diff > max_diff) max_diff = diff;
    }

    /* Y-axis 2nd-order critically damped harmonic oscillator physics */
    const float k_spring_y = 200.0f;
    const float c_damping_y = 28.28f; /* 2 * sqrt(k_spring_y) */
    float max_headroom = (float)state->geometry.headroom_y;
    if (max_headroom <= 0.0f) {
        max_headroom = (float)TE_HOVER_HEADROOM_BASE_PX;
    }

    bool all_y_settled = true;
    for (int i = 0; i < icon_count; i++) {
        float forceOffsetY = -k_spring_y * (state->anim[i].currentOffsetY - state->anim[i].targetOffsetY)
                             - c_damping_y * state->anim[i].velocityOffsetY;
        state->anim[i].velocityOffsetY += forceOffsetY * dt;
        state->anim[i].currentOffsetY += state->anim[i].velocityOffsetY * dt;

        /* Honor headroom constraint: prevent clipping beyond headroom_y */
        if (state->anim[i].currentOffsetY < -max_headroom) {
            state->anim[i].currentOffsetY = -max_headroom;
            if (state->anim[i].velocityOffsetY < 0.0f) {
                state->anim[i].velocityOffsetY = 0.0f;
            }
        }

        /* Prevent dipping below resting baseline floor */
        if (state->anim[i].currentOffsetY > 0.0f) {
            state->anim[i].currentOffsetY = 0.0f;
            state->anim[i].velocityOffsetY = 0.0f;
        }

        /* Check settlement for Y */
        if (fabsf(state->anim[i].currentOffsetY - state->anim[i].targetOffsetY) > 0.5f ||
            fabsf(state->anim[i].velocityOffsetY) > 1.0f) {
            all_y_settled = false;
        } else {
            state->anim[i].currentOffsetY = state->anim[i].targetOffsetY;
            state->anim[i].velocityOffsetY = 0.0f;
        }
    }

    /* 3D Tilt target angle calculation (pitch & yaw tilting toward cursor) */
    float max_tilt_deg = state->config.max_tilt_angle;
    if (max_tilt_deg < 0.0f) max_tilt_deg = 0.0f;
    if (max_tilt_deg > 45.0f) max_tilt_deg = 45.0f;
    const float deg2rad = 3.14159265358979323846f / 180.0f;
    const float MAX_TILT_RAD = state->config.tilt_enabled ? (max_tilt_deg * deg2rad) : 0.0f;
    float tilt_radius = (float)state->config.radius;
    if (tilt_radius <= 0.0f) tilt_radius = 120.0f;

    for (int i = 0; i < icon_count; i++) {
        if (state->mouse.is_in_taskbar && state->config.tilt_enabled) {
            float dx = state->mouse.cursor_x - state->anim[i].center_x;
            float dy = state->mouse.cursor_y - state->anim[i].center_y;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist < tilt_radius) {
                float factor = 1.0f - (dist / tilt_radius);
                // Pitch: rotation around X axis tilted toward cursor vertically
                state->anim[i].target_tilt_x = (dy / tilt_radius) * MAX_TILT_RAD * factor;
                // Yaw: rotation around Y axis tilted toward cursor horizontally
                state->anim[i].target_tilt_y = (dx / tilt_radius) * MAX_TILT_RAD * factor;
            } else {
                state->anim[i].target_tilt_x = 0.0f;
                state->anim[i].target_tilt_y = 0.0f;
            }
        } else {
            state->anim[i].target_tilt_x = 0.0f;
            state->anim[i].target_tilt_y = 0.0f;
        }
    }

    /* 3D Tilt 2nd-order critically damped harmonic oscillator physics */
    const float k_spring_tilt = 250.0f;
    const float c_damping_tilt = 31.62f; /* 2 * sqrt(k_spring_tilt) */

    bool all_tilt_settled = true;
    for (int i = 0; i < icon_count; i++) {
        float force_tilt_x = -k_spring_tilt * (state->anim[i].current_tilt_x - state->anim[i].target_tilt_x)
                             - c_damping_tilt * state->anim[i].velocity_tilt_x;
        state->anim[i].velocity_tilt_x += force_tilt_x * dt;
        state->anim[i].current_tilt_x += state->anim[i].velocity_tilt_x * dt;

        float force_tilt_y = -k_spring_tilt * (state->anim[i].current_tilt_y - state->anim[i].target_tilt_y)
                             - c_damping_tilt * state->anim[i].velocity_tilt_y;
        state->anim[i].velocity_tilt_y += force_tilt_y * dt;
        state->anim[i].current_tilt_y += state->anim[i].velocity_tilt_y * dt;

        if (fabsf(state->anim[i].current_tilt_x - state->anim[i].target_tilt_x) > 0.001f ||
            fabsf(state->anim[i].velocity_tilt_x) > 0.01f ||
            fabsf(state->anim[i].current_tilt_y - state->anim[i].target_tilt_y) > 0.001f ||
            fabsf(state->anim[i].velocity_tilt_y) > 0.01f) {
            all_tilt_settled = false;
        } else {
            state->anim[i].current_tilt_x = state->anim[i].target_tilt_x;
            state->anim[i].velocity_tilt_x = 0.0f;
            state->anim[i].current_tilt_y = state->anim[i].target_tilt_y;
            state->anim[i].velocity_tilt_y = 0.0f;
        }
    }

    /* Validate geometry generation */
    int generation_mismatch = 0;
    for (int i = 0; i < icon_count; i++) {
        if (state->anim[i].geometry_generation != state->geometry.generation) {
            generation_mismatch = 1;
            break;
        }
    }

    if (generation_mismatch) {
        /* Drop frame during resize transition to prevent floating artifacts */
        return;
    }

    /* Compute displaced positions */
    float target_pos_x[TE_HOVER_MAX_ICONS];
    float pos_y[TE_HOVER_MAX_ICONS];
    ComputeDisplacedPositions(state->anim, icon_count, state->mouse.cursor_x,
                              drag_active, state->mouse.is_in_taskbar,
                              (float)state->config.radius,
                              state->config.drop_zone_push,
                              target_pos_x, pos_y);

    /* Smooth lerp of horizontal positions */
    float pos_lerp_speed = 15.0f * dt;
    if (pos_lerp_speed > 1.0f) pos_lerp_speed = 1.0f;

    float pos_x[TE_HOVER_MAX_ICONS];
    for (int i = 0; i < icon_count; i++) {
        state->anim[i].current_pos_x += (target_pos_x[i] - state->anim[i].current_pos_x) * pos_lerp_speed;
        pos_x[i] = state->anim[i].current_pos_x;
    }

    /* Update DComp transforms including 3D tilts */
    float scales[TE_HOVER_MAX_ICONS];
    float tilts_x[TE_HOVER_MAX_ICONS];
    float tilts_y[TE_HOVER_MAX_ICONS];
    for (int i = 0; i < icon_count; i++) {
        scales[i] = state->anim[i].current_scale;
        tilts_x[i] = state->anim[i].current_tilt_x;
        tilts_y[i] = state->anim[i].current_tilt_y;
    }

    TE_DCompUpdateTransforms(icon_count, scales, pos_x, pos_y, tilts_x, tilts_y);
    TE_DCompCommit();

    /* Check if settle animation is complete */
    if (!state->mouse.is_in_taskbar && max_diff < 0.001f && all_y_settled && all_tilt_settled) {
        /* Settle complete: hide overlay and stop timer */
        TE_DCompSetOverlayAlpha(0.0f);
        TE_DCompCommit();

        state->mouse.is_settling = 0;
        state->mouse.settle_progress = 1.0f;

        /* Reset all scales to exactly 1.0 and offsets/tilts to 0.0 */
        for (int i = 0; i < icon_count; i++) {
            state->anim[i].current_scale = 1.0f;
            state->anim[i].target_scale = 1.0f;
            state->anim[i].currentOffsetY = 0.0f;
            state->anim[i].targetOffsetY = 0.0f;
            state->anim[i].velocityOffsetY = 0.0f;
            state->anim[i].current_tilt_x = 0.0f;
            state->anim[i].target_tilt_x = 0.0f;
            state->anim[i].velocity_tilt_x = 0.0f;
            state->anim[i].current_tilt_y = 0.0f;
            state->anim[i].target_tilt_y = 0.0f;
            state->anim[i].velocity_tilt_y = 0.0f;
            state->anim[i].current_pos_x = 0.0f;
        }

        /* Stop timer asynchronously from within its own callback */
        if (InterlockedCompareExchange(&s_loop_active, 0, 1) == 1) {
            if (s_timer_handle) {
                DeleteTimerQueueTimer(s_timer_queue, s_timer_handle, NULL);
                s_timer_handle = NULL;
            }
            s_last_tick_qpc = 0;
            TE_LogWrite(TE_LOG_DEBUG, LOG_TAG, "Settle complete, frame loop stopped automatically");
        }
    }
}

HRESULT TE_FrameLoopStart(void)
{
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
        /* Use INVALID_HANDLE_VALUE to wait for any running callback to complete */
        DeleteTimerQueueTimer(s_timer_queue, s_timer_handle, INVALID_HANDLE_VALUE);
        s_timer_handle = NULL;
    }

    s_last_tick_qpc = 0;

    TE_LogWrite(TE_LOG_DEBUG, LOG_TAG, "Frame loop stopped");
}

int TE_FrameLoopIsActive(void)
{
    return InterlockedCompareExchange(&s_loop_active, 0, 0);
}

void TE_FrameLoopOnMouseMove(float cursor_x, float cursor_y, int is_dragging)
{
    TE_IconHoverState* state = &g_hover_state;
    AcquireSRWLockExclusive(&state->state_lock);
    state->mouse.cursor_x = cursor_x;
    state->mouse.cursor_y = cursor_y;
    state->mouse.is_in_taskbar = 1;
    state->mouse.is_dragging = is_dragging;
    state->mouse.is_settling = 0;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    state->mouse.last_mousemove_qpc = (uint64_t)now.QuadPart;
    ReleaseSRWLockExclusive(&state->state_lock);

    /* Start frame loop if not already running, or re-assert topmost if running */
    if (!TE_FrameLoopIsActive()) {
        TE_FrameLoopStart();
    } else if (state->config.keep_on_top) {
        HWND taskbar_hwnd = state->ctx ? state->ctx->taskbar_hwnd : NULL;
        TE_DCompEnsureTopmost(taskbar_hwnd);
    }
}

void TE_FrameLoopOnMouseLeave(void)
{
    TE_IconHoverState* state = &g_hover_state;
    AcquireSRWLockExclusive(&state->state_lock);
    state->mouse.is_in_taskbar = 0;
    state->mouse.is_dragging = 0;
    state->mouse.is_settling = 1;
    state->mouse.settle_progress = 0.0f;
    ReleaseSRWLockExclusive(&state->state_lock);

    /* Frame loop continues running to animate the settle */
}

HRESULT TE_FrameLoopTriggerIconBounce(int icon_index, float impulse_strength)
{
    TE_IconHoverState* state = &g_hover_state;
    if (!state->enabled) return TE_E_FAIL;

    AcquireSRWLockExclusive(&state->state_lock);
    if (icon_index < 0 || icon_index >= state->anim_count) {
        ReleaseSRWLockExclusive(&state->state_lock);
        return TE_E_INVALIDARG;
    }

    /* Apply massive negative instantaneous velocity (upward impulse) */
    state->anim[icon_index].velocityOffsetY = -fabsf(impulse_strength);
    state->anim[icon_index].targetOffsetY = 0.0f;

    ReleaseSRWLockExclusive(&state->state_lock);

    /* Ensure frame loop is running and overlay is visible */
    if (!TE_FrameLoopIsActive()) {
        TE_FrameLoopStart();
    } else {
        TE_DCompSetOverlayAlpha(1.0f);
        TE_DCompCommit();
    }

    return TE_S_OK;
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

    int icon_count = state->anim_count;
    POINT pt = { (LONG)cursor_x, (LONG)cursor_y };

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
