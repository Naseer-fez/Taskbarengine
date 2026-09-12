#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "composition_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TE_MAX_TRACKED_TASKBARS 16

/**
 * Representation of a tracked taskbar window on a monitor.
 */
typedef struct TE_TrackedTaskbar {
    HWND hwnd;              /**< Window handle (Shell_TrayWnd or Shell_SecondaryTrayWnd). */
    HMONITOR monitor;       /**< Monitor hosting this taskbar instance. */
    bool is_primary;        /**< True if primary taskbar (Shell_TrayWnd). */
    bool policy_applied;    /**< True if accent policy is currently applied. */
} TE_TrackedTaskbar;

/**
 * State tracking for multi-monitor taskbars and debounced discovery.
 */
typedef struct TE_TrayDiscoveryState {
    TE_TrackedTaskbar taskbars[TE_MAX_TRACKED_TASKBARS];
    uint32_t count;
    UINT_PTR debounce_timer_id;
    ACCENT_POLICY current_policy;
    bool apply_secondary;
} TE_TrayDiscoveryState;

void TE_TrayDiscoveryInit(TE_TrayDiscoveryState* state);
uint32_t TE_TrayDiscoverAll(TE_TrayDiscoveryState* state);
void TE_TrayApplyPolicyToAll(TE_TrayDiscoveryState* state, const ACCENT_POLICY* policy);
void TE_TrayRestoreAll(TE_TrayDiscoveryState* state);
void TE_TrayHandleDisplayChange(TE_TrayDiscoveryState* state, const ACCENT_POLICY* policy);
void TE_TrayCleanup(TE_TrayDiscoveryState* state);

/* Unit testing helper to add mock windows */
bool TE_TrayDiscoveryAddTracked(TE_TrayDiscoveryState* state, HWND hwnd, bool is_primary);

#ifdef __cplusplus
}
#endif
