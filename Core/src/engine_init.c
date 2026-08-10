#include "core/engine.h"
#include "core/core_manager.h"
#include <sdk/te_log.h>
#include <sdk/te_debug_trace.h>
#include <wchar.h>
#include <stdio.h>

static volatile LONG g_engine_initialized = 0;
static HINSTANCE g_hinstance = NULL;

void TE_SetEngineInstance(HINSTANCE hinstance)
{
    g_hinstance = hinstance;
}

HRESULT TE_EngineInitialize(void)
{
    if (InterlockedCompareExchange(&g_engine_initialized, 1, 0) != 0) {
        TE_DebugTrace("[TE-DBG] TE_EngineInitialize: Already initialized, skipping\n");
        return S_OK;
    }
    TE_DebugTrace("[TE-DBG] TE_EngineInitialize: Starting first-time init\n");

    /* Defense-in-depth: only initialize inside explorer.exe */
    extern bool TE_IsExplorerProcess(void);
    if (!TE_IsExplorerProcess()) {
        TE_DebugTrace("[TE-DBG] TE_EngineInitialize: Not explorer process, aborting\n");
        InterlockedExchange(&g_engine_initialized, 0);
        return S_FALSE;
    }

    HRESULT hr = TE_CoreManagerInitPhaseA(g_hinstance);
    TE_DebugTraceFmt("[TE-DBG] TE_EngineInitialize: PhaseA returned hr=0x%08X\n", (unsigned int)hr);
    /* Never reset g_engine_initialized on failure — the thread-targeted
     * CBT hook ensures we are on the correct thread.  Resetting the flag
     * previously caused an unbounded retry storm that starved Explorer's
     * XAML island startup.  Explorer-restart recovery will re-install the
     * hook via TE_CrashRecoveryStart which handles the retry externally. */
    return hr;
}

HRESULT TE_EngineInitializeDeferred(void)
{
    return TE_CoreManagerInitPhaseB();
}
