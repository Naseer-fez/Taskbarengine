#include "core/fault_isolation.h"
#include "core/core_manager.h"
#include <windows.h>
#include <sdk/te_log.h>

#ifndef _MSC_VER
#include <setjmp.h>

static __thread jmp_buf g_veh_jmp;
static __thread volatile LONG g_in_veh_guarded_call = 0;
static PVOID g_veh_handle = NULL;
static SRWLOCK g_veh_init_lock = SRWLOCK_INIT;

static LONG WINAPI VehExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    if (g_in_veh_guarded_call && pExceptionInfo && pExceptionInfo->ExceptionRecord) {
        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
            code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_DATATYPE_MISALIGNMENT) {
            longjmp(g_veh_jmp, 1);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void EnsureVehHandlerRegistered(void)
{
    if (!g_veh_handle) {
        AcquireSRWLockExclusive(&g_veh_init_lock);
        if (!g_veh_handle) {
            g_veh_handle = AddVectoredExceptionHandler(1, VehExceptionHandler);
        }
        ReleaseSRWLockExclusive(&g_veh_init_lock);
    }
}
#endif

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

static void DisableFaultedPlugin(TE_PluginEntry* entry, bool invoke_disable)
{
    if (!entry || entry->fault_count < TE_MAX_FAULT_STRIKES) return;

    TE_LogWrite(TE_LOG_ERROR, "Plugin '%s' exceeded max fault strikes (%u). Disabling plugin.",
                (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown",
                TE_MAX_FAULT_STRIKES);

    const bool needs_cleanup = !entry->disabled_by_fault;
    entry->disabled_by_fault = true;
    if (needs_cleanup && invoke_disable && entry->iface && entry->iface->Disable) {
        /* Run Disable once to revert any partial visual or behavioral changes.
         * A failing Disable cannot recurse because disabled_by_fault is set first. */
        TE_FaultIsolationCallPlugin(entry, entry->iface->Disable, "Disable");
    }
    entry->enabled = false;
}

HRESULT TE_FaultIsolationCallPlugin(TE_PluginEntry* entry, HRESULT (*callback)(void), const char* callback_name)
{
    if (!entry || !callback) return E_POINTER;
    if (entry->disabled_by_fault) {
        bool is_cleanup = (entry->iface && (callback == entry->iface->Disable || callback == entry->iface->Shutdown));
        if (!is_cleanup) {
            return E_ABORT;
        }
    }

    uint32_t prev_plugin_id = TE_CoreManagerGetCurrentPluginId();
    uint32_t plugin_id = (entry && entry->context) ? (uint32_t)(uintptr_t)entry->context->core_opaque + 1 : 0;
    if (plugin_id > 0) {
        TE_CoreManagerSetCurrentPluginId(plugin_id);
    }

    WatchdogContext wd_ctx = { 0 };
    HANDLE htimer = NULL;

    BOOL timer_created = CreateTimerQueueTimer(&htimer, NULL, WatchdogTimerCallback, &wd_ctx, TE_WATCHDOG_INIT_TIMEOUT_MS, 0, WT_EXECUTEONLYONCE);

    HRESULT hr = E_FAIL;
    bool caught_exception = false;

#ifdef _MSC_VER
    __try {
        hr = callback();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        caught_exception = true;
        DWORD code = GetExceptionCode();
        TE_LogWrite(TE_LOG_ERROR, "SEH Exception (0x%08X) caught during %s in plugin '%s'",
                    (unsigned int)code, callback_name ? callback_name : "callback",
                    (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown");
    }
#else
    EnsureVehHandlerRegistered();
    g_in_veh_guarded_call = 1;
    if (setjmp(g_veh_jmp) == 0) {
        hr = callback();
    } else {
        caught_exception = true;
        TE_LogWrite(TE_LOG_ERROR, "VEH Exception caught during %s in plugin '%s'",
                    callback_name ? callback_name : "callback",
                    (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown");
    }
    g_in_veh_guarded_call = 0;
#endif

    if (timer_created && htimer) {
        DeleteTimerQueueTimer(NULL, htimer, INVALID_HANDLE_VALUE);
    }

    if (plugin_id > 0) {
        TE_CoreManagerSetCurrentPluginId(prev_plugin_id);
    }

    if (caught_exception || wd_ctx.fired) {
        entry->fault_count++;
        if (wd_ctx.fired) {
            TE_LogWrite(TE_LOG_WARN, "Watchdog timeout (%dms) during %s in plugin '%s' (strike %u/%u)",
                        TE_WATCHDOG_INIT_TIMEOUT_MS, callback_name ? callback_name : "callback",
                        (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown",
                        entry->fault_count, TE_MAX_FAULT_STRIKES);
        }

        DisableFaultedPlugin(entry, callback != entry->iface->Disable && callback != entry->iface->Shutdown);

        return caught_exception ? E_FAIL : E_ABORT;
    }

    /* On clean execution, reset consecutive strike counter if not disabled by fault */
    if (!entry->disabled_by_fault) {
        entry->fault_count = 0;
    }
    return hr;
}

HRESULT TE_FaultIsolationCallPluginInit(TE_PluginEntry* entry, HRESULT (*callback)(const PluginContext*), const PluginContext* ctx)
{
    if (!entry || !callback || !ctx) return E_POINTER;
    if (entry->disabled_by_fault) {
        return E_ABORT;
    }

    uint32_t prev_plugin_id = TE_CoreManagerGetCurrentPluginId();
    uint32_t plugin_id = (entry && entry->context) ? (uint32_t)(uintptr_t)entry->context->core_opaque + 1 : 0;
    if (plugin_id > 0) {
        TE_CoreManagerSetCurrentPluginId(plugin_id);
    }

    WatchdogContext wd_ctx = { 0 };
    HANDLE htimer = NULL;

    BOOL timer_created = CreateTimerQueueTimer(&htimer, NULL, WatchdogTimerCallback, &wd_ctx, TE_WATCHDOG_INIT_TIMEOUT_MS, 0, WT_EXECUTEONLYONCE);

    HRESULT hr = E_FAIL;
    bool caught_exception = false;

#ifdef _MSC_VER
    __try {
        hr = callback(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        caught_exception = true;
        DWORD code = GetExceptionCode();
        TE_LogWrite(TE_LOG_ERROR, "SEH Exception (0x%08X) caught during Initialize in plugin '%s'",
                    (unsigned int)code,
                    (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown");
    }
#else
    EnsureVehHandlerRegistered();
    g_in_veh_guarded_call = 1;
    if (setjmp(g_veh_jmp) == 0) {
        hr = callback(ctx);
    } else {
        caught_exception = true;
        TE_LogWrite(TE_LOG_ERROR, "VEH Exception caught during Initialize in plugin '%s'",
                    (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown");
    }
    g_in_veh_guarded_call = 0;
#endif

    if (timer_created && htimer) {
        DeleteTimerQueueTimer(NULL, htimer, INVALID_HANDLE_VALUE);
    }

    if (plugin_id > 0) {
        TE_CoreManagerSetCurrentPluginId(prev_plugin_id);
    }

    if (caught_exception || wd_ctx.fired) {
        entry->fault_count++;
        if (wd_ctx.fired) {
            TE_LogWrite(TE_LOG_WARN, "Watchdog timeout (%dms) during Initialize in plugin '%s' (strike %u/%u)",
                        TE_WATCHDOG_INIT_TIMEOUT_MS,
                        (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown",
                        entry->fault_count, TE_MAX_FAULT_STRIKES);
        }

        DisableFaultedPlugin(entry, true);

        return caught_exception ? E_FAIL : E_ABORT;
    }

    entry->fault_count = 0;
    return hr;
}

HRESULT TE_FaultIsolationCallEventCallback(TE_PluginEntry* entry, TE_EventCallback callback, TE_EventType type, const void* event_data, void* user_data)
{
    if (!callback) return E_POINTER;
    if (entry && entry->disabled_by_fault) {
        return E_ABORT;
    }

    uint32_t prev_plugin_id = TE_CoreManagerGetCurrentPluginId();
    uint32_t plugin_id = (entry && entry->context) ? (uint32_t)(uintptr_t)entry->context->core_opaque + 1 : 0;
    if (plugin_id > 0) {
        TE_CoreManagerSetCurrentPluginId(plugin_id);
    }

    WatchdogContext wd_ctx = { 0 };
    HANDLE htimer = NULL;

    BOOL timer_created = CreateTimerQueueTimer(&htimer, NULL, WatchdogTimerCallback, &wd_ctx, TE_WATCHDOG_TIMEOUT_MS, 0, WT_EXECUTEONLYONCE);

    bool caught_exception = false;

#ifdef _MSC_VER
    __try {
        callback(type, event_data, user_data);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        caught_exception = true;
        DWORD code = GetExceptionCode();
        TE_LogWrite(TE_LOG_ERROR, "SEH Exception (0x%08X) caught in event callback (type %d) for plugin '%s'",
                    (unsigned int)code, (int)type,
                    (entry && entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown");
    }
#else
    EnsureVehHandlerRegistered();
    g_in_veh_guarded_call = 1;
    if (setjmp(g_veh_jmp) == 0) {
        callback(type, event_data, user_data);
    } else {
        caught_exception = true;
        TE_LogWrite(TE_LOG_ERROR, "VEH Exception caught in event callback (type %d) for plugin '%s'",
                    (int)type,
                    (entry && entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown");
    }
    g_in_veh_guarded_call = 0;
#endif

    if (timer_created && htimer) {
        DeleteTimerQueueTimer(NULL, htimer, INVALID_HANDLE_VALUE);
    }

    if (plugin_id > 0) {
        TE_CoreManagerSetCurrentPluginId(prev_plugin_id);
    }

    if (entry) {
        if (caught_exception || wd_ctx.fired) {
            entry->fault_count++;
            if (wd_ctx.fired) {
                TE_LogWrite(TE_LOG_WARN, "Watchdog timeout (%dms) during event callback (type %d) in plugin '%s' (strike %u/%u)",
                            TE_WATCHDOG_TIMEOUT_MS, (int)type,
                            (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown",
                            entry->fault_count, TE_MAX_FAULT_STRIKES);
            }

            DisableFaultedPlugin(entry, true);

            return caught_exception ? E_FAIL : E_ABORT;
        }

        entry->fault_count = 0;
    }

    return caught_exception ? E_FAIL : S_OK;
}

