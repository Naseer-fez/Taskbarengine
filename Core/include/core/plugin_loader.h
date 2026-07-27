#pragma once

#include <sdk/te_types.h>
#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_MAX_PLUGINS 32

typedef struct TE_PluginEntry {
    HMODULE                  dll_handle;
    const PluginInterface*   interface;
    const PluginMetadata*    metadata;
    PluginContext*           context;          /* heap-allocated, per-plugin */
    bool                     enabled;
    uint32_t                 fault_count;      /* consecutive timeout counter */
    bool                     disabled_by_fault;
} TE_PluginEntry;

HRESULT TE_PluginLoaderScan(const wchar_t* modules_dir, TE_PluginEntry* registry, uint32_t* count);
HRESULT TE_PluginLoaderEnable(TE_PluginEntry* entry);
HRESULT TE_PluginLoaderDisable(TE_PluginEntry* entry);
HRESULT TE_PluginLoaderShutdown(TE_PluginEntry* entry);
void TE_PluginLoaderUnloadAll(TE_PluginEntry* registry, uint32_t count);

#ifdef __cplusplus
}
#endif
