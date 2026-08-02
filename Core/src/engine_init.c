#include "core/engine.h"
#include "core/core_manager.h"
#include <sdk/te_log.h>
#include <wchar.h>

static volatile LONG g_engine_initialized = 0;
static HINSTANCE g_hinstance = NULL;

void TE_SetEngineInstance(HINSTANCE hinstance)
{
    g_hinstance = hinstance;
}

HRESULT TE_EngineInitialize(void)
{
    if (InterlockedCompareExchange(&g_engine_initialized, 1, 0) != 0) {
        return S_OK;
    }

    return TE_CoreManagerInit(g_hinstance);
}
