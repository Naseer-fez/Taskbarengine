#include "core/engine.h"
#include "core/core_manager.h"
#include <sdk/te_log.h>
#include <wchar.h>

static bool g_engine_initialized = false;
static HINSTANCE g_hinstance = NULL;

void TE_SetEngineInstance(HINSTANCE hinstance)
{
    g_hinstance = hinstance;
}

HRESULT TE_EngineInitialize(void)
{
    if (g_engine_initialized) {
        return S_OK;
    }

    g_engine_initialized = true;
    return TE_CoreManagerInit(g_hinstance);
}
