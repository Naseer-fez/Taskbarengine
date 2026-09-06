#ifndef TE_ICON_CAPTURE_H
#define TE_ICON_CAPTURE_H

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the icon bitmap capture and caching subsystem.
 * Must be called before any other TE_IconCapture functions.
 *
 * @return TE_S_OK on success, TE_E_FAIL if initialization fails.
 *
 * @note Thread Safety: Must be called from the UI thread.
 */
HRESULT TE_IconCaptureInit(void);

/**
 * Shut down the icon capture subsystem and release all cached bitmaps.
 *
 * @note Thread Safety: Must be called from the UI thread.
 */
void TE_IconCaptureShutdown(void);

/**
 * Retrieve a cached high-resolution (256x256) icon bitmap for a taskbar app.
 *
 * On first call for a given app_id, extracts the jumbo icon from the system
 * image list via SHGetImageList(SHIL_JUMBO) and caches the result.
 * Subsequent calls return the cached bitmap.
 *
 * @param app_id     Application identifier string (from UIA discovery).
 * @param icon_index Index in the system image list.
 * @param out_bitmap Pointer to receive the HBITMAP handle.
 *                   The caller must NOT call DeleteObject on this handle;
 *                   it is owned by the cache.
 * @return TE_S_OK on success, TE_E_FAIL on extraction error,
 *         TE_E_INVALIDARG on NULL params.
 *
 * @note Thread Safety: Must be called from the UI thread.
 */
HRESULT TE_IconCaptureGetBitmap(const wchar_t* app_id, int icon_index, HBITMAP* out_bitmap);

/**
 * Invalidate all cached icon bitmaps, forcing re-extraction on next request.
 * Called when taskbar icons change (e.g., on TE_EVENT_SHELL_HOOK).
 *
 * @note Thread Safety: Must be called from the UI thread.
 * @note Performance: Completes in < 5 ms for typical icon counts (< 20).
 */
void TE_IconCaptureInvalidate(void);

#ifdef __cplusplus
}
#endif

#endif /* TE_ICON_CAPTURE_H */
