#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "composition_api.h"
#include "tray_discovery.h"
#include "transparency_config.h"
#include <sdk/te_plugin.h>
#include <sdk/te_events.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encapsulated runtime state of the taskbar_transparency plugin.
 */
typedef struct TE_TransparencyPluginState {
    const PluginContext* ctx;
    TE_TransparencySettings settings;
    TE_TrayDiscoveryState tray_state;
    SRWLOCK lock;
    bool initialized;
    bool enabled;
} TE_TransparencyPluginState;

/**
 * Retrieves the module's plugin interface entry point.
 */
const PluginInterface* TE_TransparencyGetPluginInterface(void);

/**
 * Retrieves the global plugin interface entry point (DLL export).
 */
TE_EXPORT const PluginInterface* GetPluginInterface(void);

/**
 * Accessor for active plugin state (useful for diagnostics and unit tests).
 */
const TE_TransparencyPluginState* TE_TransparencyGetState(void);

#ifdef __cplusplus
}
#endif
