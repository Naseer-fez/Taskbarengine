#include "frame_loop.h"
#include "dcomp_overlay.h"
#include "icon_layout.h"
#include <dwmapi.h>
#include <windows.h>
#include <sdk/te_log.h>
#include <sdk/te_debug_trace.h>
#include <sdk/te_easing.h>
#include <math.h>

static TE_IconHoverState* g_state = nullptr;
static HANDLE g_timer_queue = NULL;
static HANDLE g_timer = NULL;
static HANDLE g_stop_event = NULL;
static volatile LONG g_running = 0;
static volatile LONG g_timer_active = 0;

/* Frame timing stats */
static float g_frame_times_ms[120] = {};
static int g_frame_index = 0;
static int g_frame_count_total = 0;
static LARGE_INTEGER g_last_frame_qpc = {};

static SRWLOCK g_timer_lock = SRWLOCK_INIT;

static void CALLBACK FrameLoopCallback(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
    (void)lpParam;
    (void)TimerOrWaitFired;

    if (!g_state || !g_running) return;

    if (WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0) {
        return;
    }

    POINT pt;
    if (!GetCursorPos(&pt)) return;

    bool in_taskbar = PtInRect(&g_state->taskbar_rect, pt);
    bool is_settling = (g_state->settling == 1);

    /* If mouse is outside taskbar and not settling, deactivate timer to maintain 0% CPU */
    if (!in_taskbar && !g_state->was_in_taskbar && !is_settling) {
        TE_FrameLoopDeactivate();
        return;
    }

    LARGE_INTEGER now_qpc, freq;
    QueryPerformanceCounter(&now_qpc);
    QueryPerformanceFrequency(&freq);

    if (g_last_frame_qpc.QuadPart != 0) {
        float ms = (float)(now_qpc.QuadPart - g_last_frame_qpc.QuadPart) * 1000.0f / (float)freq.QuadPart;
        g_frame_times_ms[g_frame_index] = ms;
        g_frame_index = (g_frame_index + 1) % 120;
        if (g_frame_count_total < 120) g_frame_count_total++;

        if (g_frame_count_total > 0 && (g_frame_index % 15 == 0)) {
            float min_ms = g_frame_times_ms[0], max_ms = g_frame_times_ms[0], sum_ms = 0.0f;
            for (int i = 0; i < g_frame_count_total; i++) {
                float t = g_frame_times_ms[i];
                if (t < min_ms) min_ms = t;
                if (t > max_ms) max_ms = t;
                sum_ms += t;
            }
            float avg_ms = sum_ms / g_frame_count_total;
            float fps = (avg_ms > 0.0f) ? (1000.0f / avg_ms) : 0.0f;

            if (g_state->ctx.publish_state) {
                StateValue v_fps;
                v_fps.type = TE_STATE_TYPE_FLOAT;
                v_fps.value.f = fps;
                StateValue v_avg;
                v_avg.type = TE_STATE_TYPE_FLOAT;
                v_avg.value.f = avg_ms;
                StateValue v_min;
                v_min.type = TE_STATE_TYPE_FLOAT;
                v_min.value.f = min_ms;
                StateValue v_max;
                v_max.type = TE_STATE_TYPE_FLOAT;
                v_max.value.f = max_ms;
                g_state->ctx.publish_state("perf.fps", &v_fps);
                g_state->ctx.publish_state("perf.avg_ms", &v_avg);
                g_state->ctx.publish_state("perf.min_ms", &v_min);
                g_state->ctx.publish_state("perf.max_ms", &v_max);
            }
        }
    }
    g_last_frame_qpc = now_qpc;

    /* Wait for vsync when active */
    DwmFlush();

    AcquireSRWLockExclusive(&g_state->icon_lock);

    int count = g_state->icon_count;
    if (count <= 0) {
        ReleaseSRWLockExclusive(&g_state->icon_lock);
        return;
    }

    /* Compute base icon center coordinates */
    for (int i = 0; i < count; i++) {
        g_state->base_centers_x[i] = (float)g_state->icons[i].bounds.left + 
            ((float)g_state->icons[i].bounds.right - (float)g_state->icons[i].bounds.left) / 2.0f;
    }

    /* Handle mouse leaving taskbar: trigger settle transition */
    if (g_state->was_in_taskbar && !in_taskbar && !is_settling) {
        g_state->settling = 1;
        is_settling = true;
        QueryPerformanceCounter(&g_state->settle_start_qpc);
        for (int i = 0; i < count; i++) {
            g_state->settle_start_scales[i] = g_state->scales[i];
        }
    }
    g_state->was_in_taskbar = in_taskbar;

    if (in_taskbar) {
        g_state->settling = 0;
        TE_MagnifyComputeScales((float)pt.x, g_state->base_centers_x, g_state->scales, 
                                count, (float)g_state->radius, g_state->max_scale, g_state->curve);
        QueryPerformanceCounter(&g_state->settle_start_qpc);
    } else if (is_settling) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float elapsed_ms = (float)(now.QuadPart - g_state->settle_start_qpc.QuadPart) * 1000.0f / (float)freq.QuadPart;
        
        float progress = elapsed_ms / (float)(g_state->speed_ms > 0 ? g_state->speed_ms : 150);
        if (progress >= 1.0f) {
            progress = 1.0f;
            g_state->settling = 0;
        }

        for (int i = 0; i < count; i++) {
            g_state->scales[i] = TE_LerpEased(g_state->settle_start_scales[i], 1.0f, progress, TE_EASE_OUT_CUBIC);
        }

        if (progress >= 1.0f) {
            /* Settle complete: snap scales to 1.0, update DComp one last time, then deactivate timer */
            for (int i = 0; i < count; i++) {
                g_state->scales[i] = 1.0f;
            }
            TE_LayoutComputePositions(g_state->scales, g_state->base_centers_x, 
                                      g_state->displaced_x, g_state->displaced_y, 
                                      count, g_state->icon_size, (float)g_state->taskbar_rect.bottom);
            ReleaseSRWLockExclusive(&g_state->icon_lock);
            TE_DcompUpdateVisuals(g_state);
            TE_FrameLoopDeactivate();
            return;
        }
    }

    /* Compute layout positions */
    TE_LayoutComputePositions(g_state->scales, g_state->base_centers_x, 
                              g_state->displaced_x, g_state->displaced_y, 
                              count, g_state->icon_size, (float)g_state->taskbar_rect.bottom);

    ReleaseSRWLockExclusive(&g_state->icon_lock);

    /* Update DirectComposition visuals */
    TE_DcompUpdateVisuals(g_state);
}

HRESULT TE_FrameLoopStart(TE_IconHoverState* state)
{
    TE_DebugTraceFmt("[TE-DBG] FrameLoop: Start entering state=0x%p running=%ld\n", (void*)state, g_running);
    if (g_running) return S_OK;

    g_state = state;
    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_stop_event) {
        g_state = nullptr;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    
    g_timer_queue = CreateTimerQueue();
    if (!g_timer_queue) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
        g_state = nullptr;
        return hr;
    }

    g_running = 1;
    TE_FrameLoopActivate();

    TE_DebugTrace("[TE-DBG] FrameLoop: Start complete, active loop running\n");
    return S_OK;
}

void TE_FrameLoopActivate(void)
{
    if (!g_running || !g_state || !g_timer_queue) return;

    AcquireSRWLockExclusive(&g_timer_lock);
    if (!g_timer_active) {
        g_timer_active = 1;
        CreateTimerQueueTimer(&g_timer, g_timer_queue, FrameLoopCallback, NULL, 0, 16, WT_EXECUTEDEFAULT);
    }
    ReleaseSRWLockExclusive(&g_timer_lock);
}

void TE_FrameLoopDeactivate(void)
{
    AcquireSRWLockExclusive(&g_timer_lock);
    if (g_timer_active) {
        g_timer_active = 0;
        if (g_timer && g_timer_queue) {
            HANDLE t = g_timer;
            g_timer = NULL;
            DeleteTimerQueueTimer(g_timer_queue, t, NULL);
        }
    }
    ReleaseSRWLockExclusive(&g_timer_lock);
}

void TE_FrameLoopStop(void)
{
    TE_DebugTraceFmt("[TE-DBG] FrameLoop: Stop entering running=%ld\n", g_running);
    if (!g_running) return;
    
    g_running = 0;
    if (g_stop_event) {
        SetEvent(g_stop_event);
    }

    TE_FrameLoopDeactivate();

    if (g_timer_queue) {
        DeleteTimerQueueEx(g_timer_queue, INVALID_HANDLE_VALUE);
        g_timer_queue = NULL;
    }
    
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
    }
    
    g_state = nullptr;
    TE_DebugTrace("[TE-DBG] FrameLoop: Stop complete\n");
}

void TE_FrameLoopUpdateMouse(int x, int y)
{
    (void)x;
    (void)y;
}
