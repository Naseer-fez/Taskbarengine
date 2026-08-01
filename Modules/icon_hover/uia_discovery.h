#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_MAX_TASKBAR_ICONS 64

typedef struct TE_TaskbarIcon {
    RECT bounds;
    wchar_t app_id[256];
    int icon_index;
    HMONITOR monitor;
} TE_TaskbarIcon;

/**
 * @brief Enumerate taskbar button elements using UI Automation.
 * @param taskbar_hwnd Handle to Shell_TrayWnd or Shell_SecondaryTrayWnd.
 * @param out_icons Preallocated array for TE_TaskbarIcon structures.
 * @param max_count Maximum number of icons out_icons can hold.
 * @param out_count Pointer to receive actual count of icons populated.
 * @return S_OK on success, or failure HRESULT.
 */
HRESULT TE_UiaDiscoverIcons(HWND taskbar_hwnd, TE_TaskbarIcon* out_icons, int max_count, int* out_count);

/**
 * @brief Release COM interfaces cached by UI Automation discovery module.
 */
void TE_UiaCleanup(void);

#ifdef __cplusplus
}
#endif
