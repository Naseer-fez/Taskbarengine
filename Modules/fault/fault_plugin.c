#include <sdk/te_plugin.h>
#include <stdio.h>

static const PluginMetadata G_METADATA = {
    "FaultPlugin",
    "1.0.0",
    "TaskbarEngine",
    "Faulting plugin for SEH testing",
    998,
    { 0, 0 }
};

static HRESULT FaultInitialize(const PluginContext* ctx)
{
    (void)ctx;
    return S_OK;
}

static HRESULT FaultEnable(void)
{
    /* Deliberate Access Violation */
    volatile int* ptr = NULL;
    *ptr = 42;
    return S_OK;
}

static HRESULT FaultDisable(void)
{
    return S_OK;
}

static HRESULT FaultUpdate(float deltaTime)
{
    (void)deltaTime;
    return S_OK;
}

static HRESULT FaultShutdown(void)
{
    return S_OK;
}

static const PluginMetadata* FaultGetMetadata(void)
{
    return &G_METADATA;
}

static const PluginSettings* FaultGetSettings(void)
{
    return NULL;
}

static const PluginInterface G_INTERFACE = {
    FaultInitialize,
    FaultEnable,
    FaultDisable,
    FaultUpdate,
    FaultShutdown,
    FaultGetMetadata,
    FaultGetSettings
};

TE_EXPORT const PluginInterface* GetPluginInterface(void)
{
    return &G_INTERFACE;
}
