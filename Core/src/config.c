#include "core/config.h"
#include <sdk/te_jsonc.h>
#include <sdk/te_log.h>
#include <shlobj.h>
#include <stdio.h>

static const char* DEFAULT_CONFIG_CONTENT =
"{\n"
"    \"version\": 1,\n"
"    \"core\": {\n"
"        \"log_level\": \"info\",\n"
"        \"log_to_file\": false\n"
"    },\n"
"    \"plugin\": {\n"
"        \"DummyPlugin\": {\n"
"            \"enabled\": true\n"
"        }\n"
"    }\n"
"}\n";

HRESULT TE_ConfigResolvePath(wchar_t* buf, size_t buf_len)
{
    if (!buf || buf_len < MAX_PATH) return E_POINTER;

    PWSTR local_appdata = NULL;
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &local_appdata);
    if (SUCCEEDED(hr)) {
        swprintf(buf, buf_len, L"%s\\TaskbarEngine\\config.jsonc", local_appdata);
        CoTaskMemFree(local_appdata);
        return S_OK;
    }

    wcscpy(buf, L"config.jsonc");
    return S_OK;
}

HRESULT TE_ConfigLoad(const wchar_t* path, cJSON** out_root)
{
    if (!out_root) return E_POINTER;

    wchar_t resolved_path[MAX_PATH];
    if (!path || path[0] == L'\0') {
        TE_ConfigResolvePath(resolved_path, MAX_PATH);
        path = resolved_path;
    }

    DWORD attribs = GetFileAttributesW(path);
    if (attribs == INVALID_FILE_ATTRIBUTES) {
        /* File does not exist, create directory and default file */
        wchar_t dir_path[MAX_PATH];
        wcscpy(dir_path, path);
        wchar_t* last_slash = wcsrchr(dir_path, L'\\');
        if (last_slash) {
            *last_slash = L'\0';
            CreateDirectoryW(dir_path, NULL);
        }

        HANDLE hfile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hfile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hfile, DEFAULT_CONFIG_CONTENT, (DWORD)strlen(DEFAULT_CONFIG_CONTENT), &written, NULL);
            CloseHandle(hfile);
            TE_LogWrite(TE_LOG_INFO, "Created default config file at: %ls", path);
        }
    }

    cJSON* root = TE_JsoncParse(path);
    if (!root) {
        TE_LogWrite(TE_LOG_ERROR, "Failed to parse config file at: %ls", path);
        return E_FAIL;
    }

    cJSON* ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!ver || !cJSON_IsNumber(ver) || ver->valueint < 1) {
        TE_LogWrite(TE_LOG_WARN, "Config version missing or invalid, assuming version 1");
    }

    *out_root = root;
    return S_OK;
}

const cJSON* TE_ConfigGetPluginSection(const cJSON* root, const char* plugin_name)
{
    if (!root || !plugin_name) return NULL;
    const cJSON* plugin_group = cJSON_GetObjectItemCaseSensitive(root, "plugin");
    if (!plugin_group || !cJSON_IsObject(plugin_group)) return NULL;
    return cJSON_GetObjectItemCaseSensitive(plugin_group, plugin_name);
}

const cJSON* TE_ConfigGetCoreSection(const cJSON* root)
{
    if (!root) return NULL;
    return cJSON_GetObjectItemCaseSensitive(root, "core");
}
