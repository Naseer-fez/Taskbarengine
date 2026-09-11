#ifndef TE_DCOMP_OVERLAY_H
#define TE_DCOMP_OVERLAY_H

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * @param bounds     Array of icon bounding rectangles (screen coords).
 * @param bitmaps    Array of icon HBITMAP handles (may contain NULLs).
 * @return TE_S_OK on success.
 *
 * @note Thread Safety: Must be called from the animation thread or UI thread.
 */
HRESULT TE_DCompBuildVisualTree(int count, const RECT* bounds, const HBITMAP* bitmaps);

/**
 * Update the scale and position transforms for all icon visuals.
 *
 * This is the hot-path called every frame during animation.
 * Sets IDCompositionScaleTransform and IDCompositionTranslateTransform
 * for each icon visual based on computed magnification scales.
 *
 * @param count   Number of icons.
 * @param scales  Array of scale factors (1.0 = no magnification).
 * @param pos_x   Array of X position offsets.
 * @param pos_y   Array of Y position offsets.
 * @return TE_S_OK on success.
 *
 * @note Performance: Must complete in < 500 µs for 20 icons.
 */
HRESULT TE_DCompUpdateTransforms(int count, const float* scales,
                                  const float* pos_x, const float* pos_y);

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

#ifdef __cplusplus
}
#endif

#endif /* TE_DCOMP_OVERLAY_H */
