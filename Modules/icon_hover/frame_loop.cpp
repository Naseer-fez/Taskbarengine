#include "frame_loop.h"
#include "dcomp_overlay.h"
#include "icon_layout.h"
#include <dwmapi.h>
#include <windows.h>
#include <sdk/te_log.h>
#include <sdk/te_easing.h>
#include <math.h>

static TE_IconHoverState* g_state = nullptr;
static HANDLE g_timer_queue = NULL;
static HANDLE g_timer = NULL;
static HANDLE g_stop_event = NULL;
static volatile LONG g_running = 0;
static volatile LONG g_timer_active = 0;

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

    // If mouse is outside taskbar and settle is complete, reset layout and exit (idle state - 0% CPU)
    if (!in_taskbar && !is_settling) {
        if (g_state->was_in_taskbar) {
            g_state->was_in_taskbar = false;
        }
        return;
    }

    // Wait for vsync when active
    DwmFlush();

    AcquireSRWLockExclusive(&g_state->icon_lock);

    int count = g_state->icon_count;
    if (count <= 0) {
        ReleaseSRWLockExclusive(&g_state->icon_lock);
        return;
    }

    // Compute base icon center coordinates
    for (int i = 0; i < count; i++) {
        g_state->base_centers_x[i] = (float)g_state->icons[i].bounds.left + 
            ((float)g_state->icons[i].bounds.right - (float)g_state->icons[i].bounds.left) / 2.0f;
    }

    // Handle mouse leaving taskbar: trigger settle transition
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
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        float elapsed_ms = (float)(now.QuadPart - g_state->settle_start_qpc.QuadPart) * 1000.0f / (float)freq.QuadPart;
        
        float progress = elapsed_ms / (float)(g_state->speed_ms > 0 ? g_state->speed_ms : 150);
        if (progress >= 1.0f) {
            progress = 1.0f;
            g_state->settling = 0;
        }

        for (int i = 0; i < count; i++) {
            g_state->scales[i] = TE_LerpEased(g_state->settle_start_scales[i], 1.0f, progress, TE_EASE_OUT_CUBIC);
        }
    }

    // Compute layout positions
    TE_LayoutComputePositions(g_state->scales, g_state->base_centers_x, 
                              g_state->displaced_x, g_state->displaced_y, 
                              count, g_state->icon_size, (float)g_state->taskbar_rect.bottom);

    ReleaseSRWLockExclusive(&g_state->icon_lock);

    // Update DirectComposition visuals
    TE_DcompUpdateVisuals(g_state);
}

HRESULT TE_FrameLoopStart(TE_IconHoverState* state)
{
    if (g_running) return S_OK;

    g_state = state;
    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    
    g_timer_queue = CreateTimerQueue();
    if (!g_timer_queue) return HRESULT_FROM_WIN32(GetLastError());

    g_running = 1;

    if (!CreateTimerQueueTimer(&g_timer, g_timer_queue, FrameLoopCallback, NULL, 0, 16, WT_EXECUTEDEFAULT)) {
        g_running = 0;
        DeleteTimerQueueEx(g_timer_queue, NULL);
        CloseHandle(g_stop_event);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    g_timer_active = 1;
    return S_OK;
}

void TE_FrameLoopActivate(void)
{
    InterlockedExchange(&g_timer_active, 1);
}

void TE_FrameLoopDeactivate(void)
{
    InterlockedExchange(&g_timer_active, 0);
}

void TE_FrameLoopStop(void)
{
    if (!g_running) return;
    
    g_running = 0;
    g_timer_active = 0;
    SetEvent(g_stop_event);

    if (g_timer_queue && g_timer) {
        DeleteTimerQueueTimer(g_timer_queue, g_timer, INVALID_HANDLE_VALUE);
    }
    if (g_timer_queue) {
        DeleteTimerQueueEx(g_timer_queue, INVALID_HANDLE_VALUE);
    }
    
    CloseHandle(g_stop_event);
    
    g_timer = NULL;
    g_timer_queue = NULL;
    g_stop_event = NULL;
    g_state = nullptr;
}

void TE_FrameLoopUpdateMouse(int x, int y)
{
    (void)x;
    (void)y;
}
