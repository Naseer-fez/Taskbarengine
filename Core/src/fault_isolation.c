#include "core/fault_isolation.h"
#include <windows.h>
#include <sdk/te_log.h>

typedef struct WatchdogContext {
    volatile LONG fired;
} WatchdogContext;

static VOID CALLBACK WatchdogTimerCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
    (void)TimerOrWaitFired;
    WatchdogContext* ctx = (WatchdogContext*)lpParameter;
    if (ctx) {
        InterlockedExchange(&ctx->fired, 1);
    }
}

HRESULT TE_FaultIsolationCallPlugin(TE_PluginEntry* entry, HRESULT (*callback)(void), const char* callback_name)
{
    if (!entry || !callback) return E_POINTER;
    if (entry->disabled_by_fault || !entry->enabled) {
        return E_ABORT;
    }

    WatchdogContext wd_ctx = { 0 };
    HANDLE htimer = NULL;

    /* Create one-shot watchdog timer */
    CreateTimerQueueTimer(&htimer, NULL, WatchdogTimerCallback, &wd_ctx, TE_WATCHDOG_TIMEOUT_MS, 0, WT_EXECUTEONLYONCE);

    HRESULT hr = E_FAIL;
    bool caught_exception = false;

    __try {
        hr = callback();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        caught_exception = true;
        DWORD code = GetExceptionCode();
        TE_LogWrite(TE_LOG_ERROR, "SEH Exception (0x%08X) caught during %s in plugin '%s'",
                    (unsigned int)code, callback_name ? callback_name : "callback",
                    (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown");
    }

    if (htimer) {
        DeleteTimerQueueTimer(NULL, htimer, INVALID_HANDLE_VALUE);
    }

    if (caught_exception || wd_ctx.fired) {
        entry->fault_count++;
        if (wd_ctx.fired) {
            TE_LogWrite(TE_LOG_WARN, "Watchdog timeout (%dms) during %s in plugin '%s' (strike %u/%u)",
                        TE_WATCHDOG_TIMEOUT_MS, callback_name ? callback_name : "callback",
                        (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown",
                        entry->fault_count, TE_MAX_FAULT_STRIKES);
        }

        if (entry->fault_count >= TE_MAX_FAULT_STRIKES) {
            TE_LogWrite(TE_LOG_ERROR, "Plugin '%s' exceeded max fault strikes (%u). Disabling plugin.",
                        (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown",
                        TE_MAX_FAULT_STRIKES);
            entry->disabled_by_fault = true;
            TE_PluginLoaderDisable(entry);
        }

        return caught_exception ? E_FAIL : E_ABORT;
    }

    /* On clean execution, reset consecutive strike counter */
    entry->fault_count = 0;
    return hr;
}
