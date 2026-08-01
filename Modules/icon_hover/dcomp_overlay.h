#pragma once

#include "uia_discovery.h"
#include "icon_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and create DirectComposition overlay child window on target monitor/taskbar.
 */
HRESULT TE_DCompOverlayCreate(HWND taskbar_hwnd, HMONITOR monitor);

/**
 * @brief Rebuild visual tree for discovered icons and their bitmap textures.
 */
HRESULT TE_DCompOverlaySetIcons(const TE_TaskbarIcon* icons, const TE_IconEntry* bitmaps, int count);

/**
 * @brief Update transform scales and translations for icon visuals.
 */
HRESULT TE_DCompOverlaySetScales(const float scales[], int count);

/**
 * @brief Commit DirectComposition visual tree transaction.
 */
HRESULT TE_DCompOverlayCommit(void);

/**
 * @brief Show or hide DirectComposition overlay window.
 */
HRESULT TE_DCompOverlaySetVisible(bool visible);

/**
 * @brief Destroy DirectComposition overlay window and release graphics resources.
 */
void TE_DCompOverlayDestroy(void);

#ifdef __cplusplus
}
#endif
