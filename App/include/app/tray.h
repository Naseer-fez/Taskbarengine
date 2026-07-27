#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WM_TRAYICON (WM_USER + 1)
#define IDI_APPICON 101

/**
 * @brief Creates and registers the system tray icon.
 * @param hwnd_parent Hidden host window handle to receive tray notifications.
 * @param icon Handle to the application icon.
 * @return S_OK on success, HRESULT error code on failure.
 * @note Thread safety: Must be called on the UI thread owning hwnd_parent.
 */
HRESULT TE_TrayCreate(HWND hwnd_parent, HICON icon);

/**
 * @brief Removes the system tray icon and frees associated resources.
 * @return S_OK on success, HRESULT error code on failure.
 * @note Thread safety: Must be called on the UI thread owning hwnd_parent.
 */
HRESULT TE_TrayDestroy(void);

#ifdef __cplusplus
}
#endif
