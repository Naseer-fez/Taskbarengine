#include "frame_loop.h"
#include "dcomp_overlay.h"
#include <dwmapi.h>
#include <windows.h>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "dwmapi.lib")
#endif

static HANDLE g_timer_queue = NULL;
static HANDLE g_timer = NULL;
static TE_IconHoverState* g_active_state = nullptr;
static bool g_settling = false;
static DWORD g_settle_start_time = 0;
static std::vector<float> g_start_settle_scales;

static void CALLBACK FrameLoopTimerCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
    (void)TimerOrWaitFired;
    TE_IconHoverState* state = (TE_IconHoverState*)lpParameter;
    if (!state || !state->enabled) return;

    POINT pt;
    GetCursorPos(&pt);

    HWND taskbar_hwnd = state->ctx.taskbar_hwnd;
    RECT rc;
    ZeroMemory(&rc, sizeof(rc));
    if (taskbar_hwnd && IsWindow(taskbar_hwnd)) {
        GetWindowRect(taskbar_hwnd, &rc);
    }

    /* Inflate bounds slightly by hover radius */
    InflateRect(&rc, state->radius, state->radius);

    bool inside = PtInRect(&rc, pt) != FALSE;

    if (inside) {
        g_settling = false;
        TE_DCompOverlaySetVisible(true);

        std::vector<float> centers(state->icon_count);
        for (int i = 0; i < state->icon_count; ++i) {
            centers[i] = (float)(state->icons[i].bounds.left + state->icons[i].bounds.right) / 2.0f;
        }

        TE_MagnifyComputeScales((float)pt.x, centers.data(), state->scales, state->icon_count,
                               (float)state->radius, state->max_scale, state->curve);

        TE_DCompOverlaySetScales(state->scales, state->icon_count);

        /* Vsync lock */
        DwmFlush();
        TE_DCompOverlayCommit();
    } else {
        if (!g_settling) {
            g_settling = true;
            g_settle_start_time = GetTickCount();
            g_start_settle_scales.assign(state->scales, state->scales + state->icon_count);
        }

        DWORD elapsed = GetTickCount() - g_settle_start_time;
        float duration = (state->speed_ms > 0) ? (float)state->speed_ms : 150.0f;
        float t = (float)elapsed / duration;

        if (t >= 1.0f) {
            /* Settle complete -> reset scales to 1.0 and stop timer */
            for (int i = 0; i < state->icon_count; ++i) {
                state->scales[i] = 1.0f;
            }
            TE_DCompOverlaySetScales(state->scales, state->icon_count);
            DwmFlush();
            TE_DCompOverlayCommit();
            TE_DCompOverlaySetVisible(false);
            TE_FrameLoopStop();
        } else {
            /* Lerp back to 1.0 */
            for (int i = 0; i < state->icon_count; ++i) {
                float start_s = (i < (int)g_start_settle_scales.size()) ? g_start_settle_scales[i] : 1.0f;
                state->scales[i] = start_s + (1.0f - start_s) * t;
            }
            TE_DCompOverlaySetScales(state->scales, state->icon_count);
            DwmFlush();
            TE_DCompOverlayCommit();
        }
    }
}

extern "C" HRESULT TE_FrameLoopStart(TE_IconHoverState* state)
{
    if (!state) return E_POINTER;
    if (g_timer != NULL) return S_OK;

    g_active_state = state;
    g_settling = false;

    if (!g_timer_queue) {
        g_timer_queue = CreateTimerQueue();
        if (!g_timer_queue) return HRESULT_FROM_WIN32(GetLastError());
    }

    BOOL ok = CreateTimerQueueTimer(&g_timer, g_timer_queue, FrameLoopTimerCallback, state,
                                    0, 8, WT_EXECUTEINTIMERTHREAD);

    return ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

extern "C" void TE_FrameLoopStop(void)
{
    if (g_timer && g_timer_queue) {
        DeleteTimerQueueTimer(g_timer_queue, g_timer, NULL);
        g_timer = NULL;
    }
    g_active_state = nullptr;
    g_settling = false;
}

extern "C" bool TE_FrameLoopIsRunning(void)
{
    return g_timer != NULL;
}
