#ifndef TE_UIA_DISCOVERY_H
#define TE_UIA_DISCOVERY_H

#include <sdk/te_types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of taskbar icon elements that can be cached. */
#define TE_UIA_MAX_ICONS 64

/**
 * Information about a single taskbar button element discovered via UI Automation.
 */
typedef struct TE_IconElementInfo {
    RECT bounds;                /**< Screen-space bounding rectangle of the button. */
    wchar_t app_id[256];       /**< Automation ID or app identifier string. */
    int icon_index;            /**< Index in the system image list for icon extraction. */
} TE_IconElementInfo;

/**
 * Cache of discovered taskbar icon elements.
 * Populated by TE_UiaDiscoverIcons and consumed by the DComp overlay
 * and magnification systems.
 */
typedef struct TE_IconElementCache {
    TE_IconElementInfo items[TE_UIA_MAX_ICONS]; /**< Array of discovered icon elements. */
    uint32_t count;                             /**< Number of valid items in the cache. */
    uint64_t last_update_qpc;                   /**< QPC timestamp of last successful update. */
} TE_IconElementCache;

/**
 * Discover taskbar button elements using UI Automation.
 *
 * Enumerates descendant button controls of the given taskbar window,
 * extracting bounding rectangles, automation IDs, and icon indices.
 * Results are cached in the provided TE_IconElementCache structure.
 *
 * Rate-limited: skips re-enumeration if called within 500ms of the last update.
 *
 * @param taskbar_hwnd Handle to Shell_TrayWnd or Shell_SecondaryTrayWnd.
 * @param out_cache    Pointer to cache structure to populate.
 * @return TE_S_OK on success, TE_E_FAIL on UIA error, TE_E_INVALIDARG on NULL params.
 *
 * @note Thread Safety: Must be called on the UI thread only.
 * @note Performance: Typical execution time is 10-50ms. Never call during frame loop.
 */
HRESULT TE_UiaDiscoverIcons(HWND taskbar_hwnd, TE_IconElementCache* out_cache);

/**
 * Force invalidation of the UIA cache, resetting the rate limiter.
 * The next call to TE_UiaDiscoverIcons will perform a full re-enumeration.
 *
 * @param cache Pointer to the cache to invalidate.
 */
void TE_UiaCacheInvalidate(TE_IconElementCache* cache);

#ifdef __cplusplus
}
#endif

#endif /* TE_UIA_DISCOVERY_H */
