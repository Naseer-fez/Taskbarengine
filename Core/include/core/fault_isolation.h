#pragma once
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum faults before a plugin is auto-disabled. */
#define TE_MAX_FAULT_COUNT 3

/** Plugin method with no arguments. */
typedef HRESULT (*TE_PluginMethodVoid)(void);

/** Plugin method receiving a PluginContext pointer. */
typedef HRESULT (*TE_PluginMethodCtx)(const void* ctx);

/**
 * Call a plugin method wrapped in SEH fault isolation.
 * If the method throws an exception, it is caught, logged, and the plugin's
 * fault counter is incremented. After 3 faults, the plugin is disabled.
 *
 * @param fault_count  Pointer to the plugin's fault counter (incremented on fault).
 * @param plugin_name  Plugin name for logging.
 * @param method_name  Method name string for logging.
 * @param method       Function pointer to invoke.
 * @return Result of method(), or TE_E_FAIL on exception.
 * @note Thread Safety: Must be called on UI thread.
 */
HRESULT TE_FaultIsolatedCall(int* fault_count, const char* plugin_name,
                              const char* method_name, TE_PluginMethodVoid method);

/**
 * Exception filter for SEH. Logs the exception and returns EXCEPTION_EXECUTE_HANDLER.
 * @param ep           Exception pointers.
 * @param plugin_name  Plugin name for logging.
 * @return EXCEPTION_EXECUTE_HANDLER.
 */
LONG TE_FaultFilter(EXCEPTION_POINTERS* ep, const char* plugin_name);

#ifdef __cplusplus
}
#endif
