#pragma once

#include <sdk/te_types.h>
#include "core/plugin_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TE_MAX_FAULT_STRIKES     3
#define TE_WATCHDOG_TIMEOUT_MS   100

HRESULT TE_FaultIsolationCallPlugin(TE_PluginEntry* entry, HRESULT (*callback)(void), const char* callback_name);

#ifdef __cplusplus
}
#endif
