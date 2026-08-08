#include "sdk/te_jsonc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip_single_line_comments(char* buffer)
{
    if (!buffer) return;

    bool in_string = false;
    bool in_comment = false;
    size_t i = 0;

    while (buffer[i] != '\0') {
        if (in_comment) {
            if (buffer[i] == '\n' || buffer[i] == '\r') {
                in_comment = false;
            } else {
                buffer[i] = ' ';
            }
        } else if (in_string) {
            if (buffer[i] == '\\' && buffer[i + 1] != '\0') {
                i += 2;
                continue;
            }
            if (buffer[i] == '"') {
                in_string = false;
            }
        } else {
            if (buffer[i] == '"') {
                in_string = true;
            } else if (buffer[i] == '/' && buffer[i + 1] == '/') {
                in_comment = true;
                buffer[i] = ' ';
                buffer[i + 1] = ' ';
                i++;
            }
        }
        i++;
    }
}

HRESULT TE_JsoncParseString(const char* json_str, cJSON** out_root)
{
    if (!json_str || !out_root) {
        return E_INVALIDARG;
    }

    *out_root = NULL;
    size_t len = strlen(json_str);
    char* copy = (char*)malloc(len + 1);
    if (!copy) {
        return E_OUTOFMEMORY;
    }
    memcpy(copy, json_str, len + 1);

    strip_single_line_comments(copy);

    cJSON* root = cJSON_Parse(copy);
    free(copy);

    if (!root) {
        return E_FAIL;
    }

    *out_root = root;
    return S_OK;
}

HRESULT TE_JsoncParse(const wchar_t* path, cJSON** out_root)
{
    if (!path || !out_root) {
        return E_INVALIDARG;
    }

    *out_root = NULL;

    HANDLE file = CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    LARGE_INTEGER file_size_large;
    if (!GetFileSizeEx(file, &file_size_large) || file_size_large.QuadPart < 0 ||
        (unsigned long long)file_size_large.QuadPart > (unsigned long long)(SIZE_MAX - 1)) {
        CloseHandle(file);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    size_t file_size = (size_t)file_size_large.QuadPart;
    char* buffer = (char*)malloc(file_size + 1);
    if (!buffer) {
        CloseHandle(file);
        return E_OUTOFMEMORY;
    }

    size_t total_read = 0;
    while (total_read < file_size) {
        DWORD request = (DWORD)((file_size - total_read) > MAXDWORD ? MAXDWORD : (file_size - total_read));
        DWORD bytes_read = 0;
        if (!ReadFile(file, buffer + total_read, request, &bytes_read, NULL)) {
            free(buffer);
            CloseHandle(file);
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (bytes_read == 0) {
            free(buffer);
            CloseHandle(file);
            return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        }
        total_read += bytes_read;
    }
    CloseHandle(file);

    buffer[total_read] = '\0';

    HRESULT hr = TE_JsoncParseString(buffer, out_root);
    free(buffer);

    return hr;
}

void TE_JsoncFree(cJSON* root)
{
    if (root) {
        cJSON_Delete(root);
    }
}

HRESULT TE_JsoncGetPlugin(const cJSON* root, const char* name, const cJSON** out_plugin)
{
    if (!root || !name || !out_plugin) {
        return E_INVALIDARG;
    }

    *out_plugin = NULL;

    const cJSON* plugins = cJSON_GetObjectItemCaseSensitive(root, "plugin");
    if (!plugins || !cJSON_IsObject(plugins)) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    const cJSON* plugin = cJSON_GetObjectItemCaseSensitive(plugins, name);
    if (!plugin) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    *out_plugin = plugin;
    return S_OK;
}
