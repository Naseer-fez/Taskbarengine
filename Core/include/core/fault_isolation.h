#pragma once

#include <sdk/te_types.h>
#include <sdk/te_events.h>
#include "core/plugin_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TE_MAX_FAULT_STRIKES     3
#define TE_WATCHDOG_TIMEOUT_MS   100

/**
 * @brief Invoke a plugin callback wrapped in SEH exception handling and 100ms watchdog timer.
 * @param entry Target plugin entry.
 * @param callback Void parameter callback pointer (Enable, Disable, Update, Shutdown).
 * @param callback_name Diagnostic string name of callback.
 * @return S_OK on success, or E_FAIL/E_ABORT on fault/timeout.
 */
HRESULT TE_FaultIsolationCallPlugin(TE_PluginEntry* entry, HRESULT (*callback)(void), const char* callback_name);

/**
 * @brief Invoke a plugin Initialize callback wrapped in SEH exception handling and 100ms watchdog timer.
 * @param entry Target plugin entry.
 * @param callback Plugin Initialize callback pointer.
 * @param ctx PluginContext pointer passed to Initialize.
 * @return S_OK on success, or E_FAIL/E_ABORT on fault/timeout.
 */
HRESULT TE_FaultIsolationCallPluginInit(TE_PluginEntry* entry, HRESULT (*callback)(const PluginContext*), const PluginContext* ctx);

/**
 * @brief Invoke an event callback wrapped in SEH exception handling and 100ms watchdog timer.
 * @param entry Target plugin entry (may be NULL).
 * @param callback Event callback pointer.
 * @param type Event type.
 * @param event_data Event payload pointer.
 * @param user_data User context pointer.
 * @return S_OK on success, or E_FAIL/E_ABORT on fault/timeout.
 */
HRESULT TE_FaultIsolationCallEventCallback(TE_PluginEntry* entry, TE_EventCallback callback, TE_EventType type, const void* event_data, void* user_data);

#ifdef __cplusplus
}
#endif
