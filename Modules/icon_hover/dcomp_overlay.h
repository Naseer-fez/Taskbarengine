#ifndef TE_DCOMP_OVERLAY_H
#define TE_DCOMP_OVERLAY_H

#include <sdk/te_types.h>
#include "uia_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

struct IDCompositionVisual;

/**
 * Create the transparent overlay window positioned over the taskbar.
 *
 * Creates a WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
 * popup window over Shell_TrayWnd. Mouse events pass through to the real taskbar.
 *
 * @param taskbar_hwnd Handle to Shell_TrayWnd.
 * @param x            Screen X origin.
 * @param y            Screen Y origin (including headroom).
 * @param width        Overlay width in pixels.
 * @param height       Overlay height in pixels (including headroom).
 * @return Handle to the created overlay window, or NULL on failure.
 *
 * @note Thread Safety: Must be called on the UI thread.
 */
HWND TE_DCompCreateOverlayWindow(HWND taskbar_hwnd, int x, int y, int width, int height);

/**
 * Move and resize the overlay window.
 *
 * @param overlay_hwnd Handle to the overlay window.
 * @param x            New screen X origin.
 * @param y            New screen Y origin.
 * @param width        New width in pixels.
 * @param height       New height in pixels.
 */
void TE_DCompMoveOverlayWindow(HWND overlay_hwnd, int x, int y, int width, int height);

/**
 * Destroy the overlay window created by TE_DCompCreateOverlayWindow.
 *
 * @param overlay_hwnd Handle to the overlay window to destroy.
 */
void TE_DCompDestroyOverlayWindow(HWND overlay_hwnd);

/**
 * Initialize the DirectComposition device and bind it to the overlay window.
 *
 * Creates IDCompositionDevice, IDCompositionTarget, and root visual.
 *
 * @param overlay_hwnd Handle to the overlay window.
 * @return TE_S_OK on success, TE_E_FAIL if DComp initialization fails.
 *
 * @note Thread Safety: Must be called on the UI thread.
 */
HRESULT TE_DCompInitDevice(HWND overlay_hwnd);

/**
 * Release all DirectComposition resources (device, target, visuals).
 */
void TE_DCompDestroyDevice(void);

/**
 * Build the visual tree with one child visual per discovered icon.
 *
 * Creates N IDCompositionVisual children under the root visual,
 * each with scale and translate transforms.
 *
 * @param count      Number of icons.
 * @param elements   Array of icon element info (contains buttonRect and glyphRect).
 * @param bitmaps    Array of icon HBITMAP handles (may contain NULLs).
 * @param baseline_y Taskbar baseline Y in screen coordinates.
 * @param overlay_x  Overlay screen X coordinate.
 * @param overlay_y  Overlay screen Y coordinate.
 * @return TE_S_OK on success.
 *
 * @note Thread Safety: Must be called from the animation thread or UI thread.
 */
HRESULT TE_DCompBuildVisualTree(int count, const TE_IconElementInfo* elements, const HBITMAP* bitmaps, int baseline_y, int overlay_x, int overlay_y);

/**
 * Update the scale, position, and 3D tilt transforms for all icon visuals.
 *
 * This is the hot-path called every frame during animation.
 * Sets IDCompositionScaleTransform, IDCompositionTranslateTransform,
 * and 3D perspective matrix transform for each icon visual based on
 * computed magnification scales and cursor tilt angles.
 *
 * @param count   Number of icons.
 * @param scales  Array of scale factors (1.0 = no magnification).
 * @param pos_x   Array of X position offsets (optional, may be NULL).
 * @param pos_y   Array of Y position offsets (optional, may be NULL).
 * @param tilt_x  Array of pitch tilt angles in radians (optional, may be NULL).
 * @param tilt_y  Array of yaw tilt angles in radians (optional, may be NULL).
 * @return TE_S_OK on success.
 *
 * @note Performance: Must complete in < 500 µs for 20 icons.
 */
HRESULT TE_DCompUpdateTransforms(int count, const float* scales,
                                  const float* pos_x, const float* pos_y,
                                  const float* tilt_x, const float* tilt_y);

/**
 * Set the overall overlay opacity.
 *
 * Used to fade the overlay in/out. Setting alpha to 0.0 causes DWM
 * to skip GPU composition entirely (zero GPU cost when idle).
 *
 * @param alpha Opacity value [0.0 = fully transparent, 1.0 = fully opaque].
 * @return TE_S_OK on success.
 */
HRESULT TE_DCompSetOverlayAlpha(float alpha);

/**
 * Commit all pending DirectComposition changes to the visual tree.
 * Must be called after UpdateTransforms or SetOverlayAlpha to present.
 *
 * @return TE_S_OK on success.
 */
HRESULT TE_DCompCommit(void);

/**
 * Handle DXGI device removal/reset by tearing down device-dependent resources
 * and reinitializing the D3D11, D2D, and DComp devices (PERF-204).
 *
 * @return TE_S_OK on successful recovery, error code otherwise.
 */
HRESULT TE_DCompHandleDeviceLoss(void);

/**
 * Ensure the overlay window remains above the taskbar in the Z-order.
 * Re-asserts HWND_TOPMOST if the taskbar window was elevated above the overlay.
 *
 * @param taskbar_hwnd Handle to Shell_TrayWnd.
 */
void TE_DCompEnsureTopmost(HWND taskbar_hwnd);

/**
 * Load a custom Start Button image file (PNG, JPG, BMP, ICO or SVG) from disk.
 * Decodes the image and converts it into a Direct2D bitmap (ID2D1Bitmap).
 *
 * @param image_path Null-terminated absolute or relative file path to the image.
 * @return TE_S_OK on success, TE_E_FAIL on failure, TE_E_INVALIDARG on NULL path.
 */
HRESULT TE_DCompLoadStartImage(const wchar_t* image_path);

/**
 * Enable or disable custom Start Button replacement in the DComp overlay.
 *
 * @param enabled Non-zero to enable, 0 to disable.
 */
void TE_DCompSetCustomStartButtonEnabled(int enabled);

/**
 * Query whether custom Start Button replacement is currently enabled.
 *
 * @return Non-zero if enabled, 0 if disabled.
 */
int TE_DCompIsCustomStartButtonEnabled(void);

/**
 * Get the current screen-space bounding rectangle of the custom Start Button visual.
 *
 * @param out_rect Pointer to RECT receiving screen coordinates.
 * @return Non-zero if valid, 0 if no custom Start button is active.
 */
int TE_DCompGetStartButtonBounds(RECT* out_rect);

#define TE_MAX_DCOMP_TARGETS 16

/**
 * Register an additional DirectComposition target for a secondary monitor overlay.
 * Creates an IDCompositionTarget bound to overlay_hwnd with its own root visual and effect group.
 *
 * @param taskbar_hwnd Associated taskbar HWND (Shell_SecondaryTrayWnd).
 * @param overlay_hwnd Overlay window HWND.
 * @param out_target_index Receives the assigned 0-based target index.
 * @return TE_S_OK on success, TE_E_FAIL or error HRESULT.
 */
HRESULT TE_DCompAddTarget(HWND taskbar_hwnd, HWND overlay_hwnd, int* out_target_index);

/**
 * Remove a DirectComposition target and destroy its visual tree.
 *
 * @param target_index 0-based target index returned by TE_DCompAddTarget.
 */
void TE_DCompRemoveTarget(int target_index);

/**
 * Get the total number of registered targets.
 */
int TE_DCompGetTargetCount(void);

/**
 * Build visual tree for a specific target.
 */
HRESULT TE_DCompBuildVisualTreeForTarget(int target_index, int count, const TE_IconElementInfo* elements, const HBITMAP* bitmaps, int baseline_y, int overlay_x, int overlay_y);

/**
 * Update transforms for a specific target.
 */
HRESULT TE_DCompUpdateTransformsForTarget(int target_index, int count, const float* scales,
                                          const float* pos_x, const float* pos_y,
                                          const float* tilt_x, const float* tilt_y);

/**
 * Set overlay alpha for a specific target.
 */
HRESULT TE_DCompSetOverlayAlphaForTarget(int target_index, float alpha);

/**
 * Get Start button bounds for a specific target.
 */
int TE_DCompGetStartButtonBoundsForTarget(int target_index, RECT* out_rect);

/**
 * Attach the dynamic island visual to the primary target's root visual.
 *
 * @param island_visual The IDCompositionVisual created by dynamic island.
 * @return TE_S_OK on success, TE_E_FAIL if root_visual is null.
 */
HRESULT TE_DCompAttachIslandVisual(struct IDCompositionVisual* island_visual);

/**
 * Detach the dynamic island visual from the primary target's root visual.
 *
 * @param island_visual The IDCompositionVisual to detach.
 */
void TE_DCompDetachIslandVisual(struct IDCompositionVisual* island_visual);

/**
 * Query whether the dynamic island visual is currently visible and requires
 * the overlay window to remain shown even if icon hover is idle.
 *
 * @return Non-zero if island is visible, 0 otherwise.
 */
int TE_DCompIsIslandVisible(void);

#ifdef __cplusplus
}
#endif

#endif /* TE_DCOMP_OVERLAY_H */
