#include "core/plugin_loader.h"
#include "core/fault_isolation.h"
#include <sdk/te_log.h>
#include <wchar.h>
#include <stdio.h>

HRESULT TE_PluginLoaderScan(const wchar_t* modules_dir, TE_PluginEntry* registry, uint32_t* count)
{
    if (!modules_dir || !registry || !count) return E_POINTER;

    *count = 0;
    ZeroMemory(registry, sizeof(TE_PluginEntry) * TE_MAX_PLUGINS);

    wchar_t search_pattern[MAX_PATH];
    swprintf(search_pattern, MAX_PATH, L"%s\\*.dll", modules_dir);

    WIN32_FIND_DATAW find_data;
    HANDLE hfind = FindFirstFileW(search_pattern, &find_data);
    if (hfind == INVALID_HANDLE_VALUE) {
        TE_LogWrite(TE_LOG_INFO, "No plugin DLLs found in directory: %ls", modules_dir);
        return S_OK;
    }

    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        if (*count >= TE_MAX_PLUGINS) {
            TE_LogWrite(TE_LOG_WARN, "Max plugins limit (%d) reached. Skipping remaining DLLs.", TE_MAX_PLUGINS);
            break;
        }

        wchar_t dll_path[MAX_PATH];
        swprintf(dll_path, MAX_PATH, L"%s\\%s", modules_dir, find_data.cFileName);

        HMODULE hdll = LoadLibraryW(dll_path);
        if (!hdll) {
            TE_LogWrite(TE_LOG_WARN, "Failed to load plugin DLL: %ls (error %lu)", dll_path, GetLastError());
            continue;
        }

        FARPROC proc = GetProcAddress(hdll, "GetPluginInterface");
        if (!proc) {
            TE_LogWrite(TE_LOG_WARN, "DLL %ls does not export GetPluginInterface", find_data.cFileName);
            FreeLibrary(hdll);
            continue;
        }

        GetPluginInterfaceFunc get_iface = (GetPluginInterfaceFunc)(uintptr_t)proc;
        const PluginInterface* iface = get_iface();
        if (!iface || !iface->GetMetadata) {
            TE_LogWrite(TE_LOG_WARN, "Plugin in %ls returned invalid interface or missing GetMetadata", find_data.cFileName);
            FreeLibrary(hdll);
            continue;
        }

        const PluginMetadata* meta = iface->GetMetadata();
        if (!meta || !meta->name) {
            TE_LogWrite(TE_LOG_WARN, "Plugin in %ls returned NULL metadata or name", find_data.cFileName);
            FreeLibrary(hdll);
            continue;
        }

        TE_PluginEntry entry = { 0 };
        entry.dll_handle = hdll;
        entry.iface = iface;
        entry.metadata = meta;
        entry.context = (PluginContext*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PluginContext));
        if (!entry.context) {
            TE_LogWrite(TE_LOG_ERROR, "Failed to allocate context for plugin '%s'", meta->name);
            FreeLibrary(hdll);
            continue;
        }
        entry.enabled = false;
        entry.fault_count = 0;
        entry.disabled_by_fault = false;

        /* Insertion sort by priority ascending (lower priority value loaded first) */
        uint32_t insert_idx = *count;
        for (uint32_t i = 0; i < *count; i++) {
            if (meta->priority < registry[i].metadata->priority) {
                insert_idx = i;
                break;
            }
        }

        for (uint32_t j = *count; j > insert_idx; j--) {
            registry[j] = registry[j - 1];
        }

        registry[insert_idx] = entry;
        (*count)++;

        TE_LogWrite(TE_LOG_INFO, "Discovered plugin '%s' (v%s) with priority %u",
                    meta->name, meta->version ? meta->version : "unknown", meta->priority);

    } while (FindNextFileW(hfind, &find_data));

    FindClose(hfind);
    return S_OK;
}

HRESULT TE_PluginLoaderEnable(TE_PluginEntry* entry)
{
    if (!entry || !entry->iface) return E_POINTER;
    if (entry->enabled) return S_OK;
    if (entry->disabled_by_fault) return E_ABORT;

    if (!entry->iface->Enable) {
        entry->enabled = true;
        return S_OK;
    }

    HRESULT hr = TE_FaultIsolationCallPlugin(entry, entry->iface->Enable, "Enable");
    if (SUCCEEDED(hr)) {
        entry->enabled = true;
        TE_LogWrite(TE_LOG_INFO, "Plugin '%s' enabled successfully", entry->metadata->name);
    } else {
        TE_LogWrite(TE_LOG_ERROR, "Failed to enable plugin '%s' (hr: 0x%08X)", entry->metadata->name, (unsigned int)hr);
    }
    return hr;
}

HRESULT TE_PluginLoaderDisable(TE_PluginEntry* entry)
{
    if (!entry || !entry->iface) return E_POINTER;
    if (!entry->enabled) return S_OK;

    if (entry->iface->Disable) {
        TE_FaultIsolationCallPlugin(entry, entry->iface->Disable, "Disable");
    }

    entry->enabled = false;
    TE_LogWrite(TE_LOG_INFO, "Plugin '%s' disabled", entry->metadata->name);
    return S_OK;
}

HRESULT TE_PluginLoaderShutdown(TE_PluginEntry* entry)
{
    if (!entry || !entry->iface) return E_POINTER;

    if (entry->enabled) {
        TE_PluginLoaderDisable(entry);
    }

    if (entry->iface->Shutdown) {
        TE_FaultIsolationCallPlugin(entry, entry->iface->Shutdown, "Shutdown");
    }

    TE_LogWrite(TE_LOG_INFO, "Plugin '%s' shutdown and unloaded", (entry->metadata && entry->metadata->name) ? entry->metadata->name : "unknown");

    if (entry->context) {
        HeapFree(GetProcessHeap(), 0, entry->context);
        entry->context = NULL;
    }

    if (entry->dll_handle) {
        FreeLibrary(entry->dll_handle);
        entry->dll_handle = NULL;
    }

    entry->iface = NULL;
    entry->metadata = NULL;
    entry->enabled = false;

    return S_OK;
}

void TE_PluginLoaderUnloadAll(TE_PluginEntry* registry, uint32_t count)
{
    if (!registry) return;

    /* Reverse priority order shutdown */
    for (int i = (int)count - 1; i >= 0; i--) {
        TE_PluginLoaderShutdown(&registry[i]);
    }
}
