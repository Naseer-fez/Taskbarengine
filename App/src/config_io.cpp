#include "config_io.h"
#include <sdk/te_jsonc.h>
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

std::wstring ConfigIO_GetConfigPath()
{
    PWSTR local_appdata = NULL;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &local_appdata);
    if (SUCCEEDED(hr)) {
        std::wstring path = std::wstring(local_appdata) + L"\\TaskbarEngine\\config.jsonc";
        CoTaskMemFree(local_appdata);
        return path;
    }
    return L"config.jsonc";
}

cJSON* ConfigIO_Load(const std::wstring& path)
{
    cJSON* root = nullptr;
    if (SUCCEEDED(TE_JsoncParse(path.c_str(), &root))) {
        return root;
    }
    return nullptr;
}

HRESULT ConfigIO_Save(const std::wstring& path, cJSON* root)
{
    if (!root) return E_POINTER;

    char* json_str = cJSON_Print(root);
    if (!json_str) return E_FAIL;

    std::wstring dir = path.substr(0, path.find_last_of(L"\\/"));
    SHCreateDirectoryExW(NULL, dir.c_str(), NULL);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        cJSON_free(json_str);
        return E_FAIL;
    }

    out.write(json_str, strlen(json_str));
    out.close();
    cJSON_free(json_str);
    return S_OK;
}

cJSON* ConfigIO_GetPluginValue(cJSON* root, const char* plugin_name, const char* key)
{
    if (!root || !plugin_name || !key) return nullptr;

    cJSON* plugin_section = cJSON_GetObjectItemCaseSensitive(root, "plugin");
    if (!plugin_section) return nullptr;

    cJSON* specific_plugin = cJSON_GetObjectItemCaseSensitive(plugin_section, plugin_name);
    if (!specific_plugin) return nullptr;

    return cJSON_GetObjectItemCaseSensitive(specific_plugin, key);
}

HRESULT ConfigIO_SetPluginValue(cJSON* root, const char* plugin_name, const char* key, cJSON* value)
{
    if (!root || !plugin_name || !key || !value) {
        if (value) cJSON_Delete(value);
        return E_POINTER;
    }

    cJSON* plugin_section = cJSON_GetObjectItemCaseSensitive(root, "plugin");
    if (!plugin_section) {
        plugin_section = cJSON_CreateObject();
        if (plugin_section) cJSON_AddItemToObject(root, "plugin", plugin_section);
    }

    if (!plugin_section) return E_OUTOFMEMORY;

    cJSON* specific_plugin = cJSON_GetObjectItemCaseSensitive(plugin_section, plugin_name);
    if (!specific_plugin) {
        specific_plugin = cJSON_CreateObject();
        if (specific_plugin) cJSON_AddItemToObject(plugin_section, plugin_name, specific_plugin);
    }

    if (!specific_plugin) return E_OUTOFMEMORY;

    // Replace if exists, add if not
    cJSON* existing = cJSON_GetObjectItemCaseSensitive(specific_plugin, key);
    if (existing) {
        cJSON_ReplaceItemInObjectCaseSensitive(specific_plugin, key, value);
    } else {
        cJSON_AddItemToObject(specific_plugin, key, value);
    }

    return S_OK;
}
