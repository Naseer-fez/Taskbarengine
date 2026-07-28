#pragma once

#include "te_types.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Engine event types.
 */
typedef enum TE_EventType {
    TE_EVENT_CONFIG_CHANGED = 1,
    TE_EVENT_DISPLAY_CHANGED,
    TE_EVENT_DPI_CHANGED,
    TE_EVENT_TASKBAR_GEOMETRY,
    TE_EVENT_SHELL_HOOK,
    TE_EVENT_POWER,
    TE_EVENT_DEVICE,
    TE_EVENT_VDESKTOP
} TE_EventType;

/**
 * @brief Event callback function signature.
 */
typedef HRESULT (*TE_EventCallback)(uint32_t type, const void* event_data, void* user_data);

/**
 * @brief Payload for TE_EVENT_CONFIG_CHANGED.
 */
typedef struct TE_ConfigChangedEvent {
    const cJSON* new_config;  /* Plugin-specific sub-object */
} TE_ConfigChangedEvent;

/**
 * @brief Payload for TE_EVENT_DISPLAY_CHANGED.
 */
typedef struct TE_DisplayChangedEvent {
    HMONITOR monitor;
    RECT monitor_rect;
} TE_DisplayChangedEvent;

/**
 * @brief Payload for TE_EVENT_DPI_CHANGED.
 */
typedef struct TE_DpiChangedEvent {
    HMONITOR monitor;
    uint32_t new_dpi;
} TE_DpiChangedEvent;

/**
 * @brief Payload for TE_EVENT_TASKBAR_GEOMETRY.
 */
typedef struct TE_TaskbarGeometryEvent {
    HWND taskbar_hwnd;
    RECT taskbar_rect;
    WINDOWPOS* window_pos;  /* Can be NULL if not triggered by WM_WINDOWPOSCHANGING */
} TE_TaskbarGeometryEvent;

/**
 * @brief Stub payloads for future events.
 */
typedef struct TE_ShellHookEvent {
    int code;
    WPARAM wparam;
    LPARAM lparam;
} TE_ShellHookEvent;

typedef struct TE_PowerEvent {
    DWORD event_type;
    void* event_data;
} TE_PowerEvent;

typedef struct TE_DeviceEvent {
    DWORD event_type;
    void* event_data;
} TE_DeviceEvent;

typedef struct TE_VDesktopEvent {
    GUID desktop_id;
} TE_VDesktopEvent;

#ifdef __cplusplus
}
#endif
