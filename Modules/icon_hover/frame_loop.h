#ifndef TE_FRAME_LOOP_H
#define TE_FRAME_LOOP_H

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the vsync-aligned animation frame loop.
 *
 * Creates a timer queue timer at ~8ms (125 Hz) that:
 * 1. Computes magnification scales from current cursor position
 * 2. Updates DComp transforms
 * 3. Commits to present
 *
 * The timer self-cancels when the settle animation completes.
 * No-op if the frame loop is already running.
 *
 * @return TE_S_OK on success, TE_E_FAIL if timer creation fails.
 */
HRESULT TE_FrameLoopStart(void);

/**
 * Stop the animation frame loop immediately.
 * Cancels the timer queue timer if active.
 */
void TE_FrameLoopStop(void);

/**
 * Check whether the frame loop timer is currently active.
 *
 * @return Non-zero if the frame loop is running.
 */
int TE_FrameLoopIsActive(void);

/**
 * Notify the frame loop that the mouse has moved.
 * Updates cursor coordinates and wakes the animation if idle.
 *
 * @param cursor_x Screen X coordinate of the cursor.
 * @param cursor_y Screen Y coordinate of the cursor.
 */
void TE_FrameLoopOnMouseMove(float cursor_x, float cursor_y);

/**
 * Notify the frame loop that the mouse has left the taskbar.
 * Initiates the settle animation (scales → 1.0 over speed_ms).
 */
void TE_FrameLoopOnMouseLeave(void);

#ifdef __cplusplus
}
#endif

#endif /* TE_FRAME_LOOP_H */
