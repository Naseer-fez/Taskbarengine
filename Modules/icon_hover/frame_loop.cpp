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
 * Compute displaced X positions when icons magnify.
 * Icons push their neighbors outward proportionally to their scale increase.
 */
static void ComputeDisplacedPositions(
    const TE_IconAnimState* anim, int count,
    float* out_pos_x, float* out_pos_y)
{
    if (count <= 0) return;

    /* Calculate cumulative displacement from left to right.
     * Each icon's width increase pushes all subsequent icons rightward. */
    float cumulative_dx = 0.0f;

    for (int i = 0; i < count; i++) {
        float scale = anim[i].current_scale;
        float base_w = anim[i].base_width;

        /* Displacement for this icon: center it around its scaled position */
        float extra_w = base_w * (scale - 1.0f);
        out_pos_x[i] = cumulative_dx - extra_w / 2.0f;

        /* Y displacement: push icon upward by half the extra height */
        float base_h = anim[i].base_height;
        float extra_h = base_h * (scale - 1.0f);
        out_pos_y[i] = -extra_h; /* Grow upward from taskbar */

        /* Accumulate displacement for subsequent icons */
        cumulative_dx += extra_w;
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

    /* Safety timeout: if no mouse movement for 500ms while not settling,
     * force settle mode */
    if (state->mouse.is_in_taskbar && state->mouse.last_mousemove_qpc > 0) {
        uint64_t elapsed_ms = (uint64_t)((now.QuadPart - (LONGLONG)state->mouse.last_mousemove_qpc)
                             * 1000 / s_qpc_freq.QuadPart);
        if (elapsed_ms > MOUSE_TIMEOUT_MS) {
            state->mouse.is_in_taskbar = 0;
            state->mouse.is_settling = 1;
            state->mouse.settle_progress = 0.0f;
        }
    }

    /* Compute target scales */
    float target_scales[TE_HOVER_MAX_ICONS];
    float icon_centers[TE_HOVER_MAX_ICONS];

    for (int i = 0; i < icon_count; i++) {
        icon_centers[i] = state->anim[i].center_x;
    }

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
    } else {
        /* Mouse left: settle targets are all 1.0 */
        for (int i = 0; i < icon_count; i++) {
            target_scales[i] = 1.0f;
        }
    }

    /* Smooth interpolation: lerp current scales toward targets */
    float lerp_speed = (state->config.speed_ms > 0)
                     ? (1000.0f / (float)state->config.speed_ms) * dt
                     : 1.0f;
    if (lerp_speed > 1.0f) lerp_speed = 1.0f;

    /* For mouse-in, use faster interpolation for responsive feel */
    float in_speed = lerp_speed * 3.0f;
    if (in_speed > 1.0f) in_speed = 1.0f;

    float max_diff = 0.0f;
    for (int i = 0; i < icon_count; i++) {
        float target = target_scales[i];
        float current = state->anim[i].current_scale;

        /* Use faster lerp when approaching target (mouse in), slower for settle */
        float speed = state->mouse.is_in_taskbar ? in_speed : lerp_speed;
        float new_scale = current + (target - current) * speed;

        /* Clamp to valid range */
        if (new_scale < 1.0f) new_scale = 1.0f;
        if (new_scale > state->config.max_scale) new_scale = state->config.max_scale;

        state->anim[i].current_scale = new_scale;
        state->anim[i].target_scale = target;

        float diff = fabsf(new_scale - target);
        if (diff > max_diff) max_diff = diff;
    }

    /* Compute displaced positions */
    float pos_x[TE_HOVER_MAX_ICONS];
    float pos_y[TE_HOVER_MAX_ICONS];
    ComputeDisplacedPositions(state->anim, icon_count, pos_x, pos_y);

    /* Update DComp transforms */
    float scales[TE_HOVER_MAX_ICONS];
    for (int i = 0; i < icon_count; i++) {
        scales[i] = state->anim[i].current_scale;
    }

    TE_DCompUpdateTransforms(icon_count, scales, pos_x, pos_y);
    TE_DCompCommit();

    /* Check if settle animation is complete */
    if (!state->mouse.is_in_taskbar && max_diff < 0.001f) {
        /* Settle complete: hide overlay and stop timer */
        TE_DCompSetOverlayAlpha(0.0f);
        TE_DCompCommit();

        state->mouse.is_settling = 0;
        state->mouse.settle_progress = 1.0f;

        /* Reset all scales to exactly 1.0 */
        for (int i = 0; i < icon_count; i++) {
            state->anim[i].current_scale = 1.0f;
            state->anim[i].target_scale = 1.0f;
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

void TE_FrameLoopOnMouseMove(float cursor_x, float cursor_y)
{
    TE_IconHoverState* state = &g_hover_state;
    state->mouse.cursor_x = cursor_x;
    state->mouse.cursor_y = cursor_y;
    state->mouse.is_in_taskbar = 1;
    state->mouse.is_settling = 0;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    state->mouse.last_mousemove_qpc = (uint64_t)now.QuadPart;

    /* Start frame loop if not already running */
    if (!TE_FrameLoopIsActive()) {
        TE_FrameLoopStart();
    }
}

void TE_FrameLoopOnMouseLeave(void)
{
    TE_IconHoverState* state = &g_hover_state;
    state->mouse.is_in_taskbar = 0;
    state->mouse.is_settling = 1;
    state->mouse.settle_progress = 0.0f;

    /* Frame loop continues running to animate the settle */
}
