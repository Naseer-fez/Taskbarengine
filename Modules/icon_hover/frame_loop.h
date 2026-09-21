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
 * @param cursor_x    Screen X coordinate of the cursor.
 * @param cursor_y    Screen Y coordinate of the cursor.
 * @param is_dragging Non-zero if drag-and-drop operation is active.
 */
void TE_FrameLoopOnMouseMove(float cursor_x, float cursor_y, int is_dragging);

/**
 * Extended mouse move handler accepting target taskbar HWND.
 */
void TE_FrameLoopOnMouseMoveEx(float cursor_x, float cursor_y, int is_dragging, HWND taskbar_hwnd);

/**
 * Notify the frame loop that the mouse has left the taskbar.
 * Initiates the settle animation (scales → 1.0 over speed_ms).
 */
void TE_FrameLoopOnMouseLeave(void);

/**
 * Trigger an instantaneous vertical bounce impulse on a specific icon.
 * Applies negative instantaneous velocity (upward impulse) to velocityOffsetY.
 *
 * @param icon_index       0-based index of the icon to bounce.
 * @param impulse_strength Upward impulse magnitude (positive value, applied as negative velocity).
 * @return TE_S_OK on success, TE_E_INVALIDARG on out-of-range index.
 */
HRESULT TE_FrameLoopTriggerIconBounce(int icon_index, float impulse_strength);

/**
 * Trigger an instantaneous vertical bounce impulse on a specific icon on a specific monitor.
 *
 * @param monitor_index    0-based monitor index.
 * @param icon_index       0-based index of the icon to bounce.
 * @param impulse_strength Upward impulse magnitude (positive value, applied as negative velocity).
 * @return TE_S_OK on success, TE_E_INVALIDARG on out-of-range index.
 */
HRESULT TE_FrameLoopTriggerIconBounceForMonitor(int monitor_index, int icon_index, float impulse_strength);

/**
 * Check if the given screen coordinate hits the custom Start button visual.
 * If hit, forwards the click to the OS to open the Start menu.
 *
 * @param cursor_x Screen X coordinate.
 * @param cursor_y Screen Y coordinate.
 * @return 1 if Start button was hit and triggered, 0 otherwise.
 */
int TE_FrameLoopCheckStartButtonClick(float cursor_x, float cursor_y);

/**
 * Forward a click or invoke event to the OS to open the Windows Start Menu.
 */
void TE_TriggerStartMenu(void);

/**
 * Dynamic Island wake callback for background media state transitions.
 * Wakes the frame animation loop so opacity/size lerping proceeds immediately.
 */
void TE_FrameLoopWakeDynamicIsland(void);

#ifdef __cplusplus
}
#endif

#endif /* TE_FRAME_LOOP_H */
