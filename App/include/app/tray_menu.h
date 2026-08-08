#pragma once

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_TRAY_MENU_SETTINGS 40001
#define TE_TRAY_MENU_ENABLE_ALL 40002
#define TE_TRAY_MENU_RELOAD_CONFIG 40003
#define TE_TRAY_MENU_ABOUT 40004
#define TE_TRAY_MENU_EXIT 40005

void TE_TrayMenuShow(HWND hwnd);
bool TE_TrayMenuHandleCommand(HWND hwnd, WPARAM wparam);
bool TE_TrayMenuOpenSettings(void);

#ifdef __cplusplus
}
#endif
