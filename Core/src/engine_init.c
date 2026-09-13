#include "core/engine_init.h"

#include <windows.h>

#include <sdk/te_types.h>
#include "core/core_manager.h"

static HWND g_active_taskbar_hwnd = NULL;

HRESULT TE_InitializeEngine(HWND taskbar_hwnd)
{
    if (!IsWindow(taskbar_hwnd)) {
        return TE_E_INVALIDARG;
    }

    g_active_taskbar_hwnd = taskbar_hwnd;

    /* Phase 2: Delegate to Core Manager for full initialization.
     * This is now called from TE_TaskbarSubclassProc on WM_TE_INIT,
     * but we keep this function as a compatibility shim. */
    return TE_CoreManagerInit(taskbar_hwnd);
}

void TE_ShutdownEngine(void)
{
    TE_CoreManagerShutdown();
    g_active_taskbar_hwnd = NULL;
}
