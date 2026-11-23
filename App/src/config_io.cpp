#include "config_io.h"
#include <sdk/te_jsonc.h>
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <cstring>

#include <vector>

std::wstring ConfigIO_GetConfigPath()
{
    // Keep the GUI on the same per-user configuration file as EngineDLL.
    // The engine intentionally prefers this location because it is writable
    // when installed under Program Files.
    wchar_t appdata[MAX_PATH] = { 0 };
    const DWORD appdata_len = GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, MAX_PATH);
    if (appdata_len > 0 && appdata_len < MAX_PATH) {
        std::wstring user_config = std::wstring(appdata) + L"\\TaskbarEngine\\config.jsonc";
        if (GetFileAttributesW(user_config.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return user_config;
        }
    }

    // Search relative to the executable path, matching CoreManager's order.
    wchar_t exe_path[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH)) {
        wchar_t* last_slash = wcsrchr(exe_path, L'\\');
        if (last_slash) {
            *last_slash = L'\0';
        }

        const wchar_t* relatives[] = {
            L"\\Config\\config.jsonc",
            L"\\default_config.jsonc",
            L"\\Config\\default_config.jsonc",
            L"\\..\\Config\\config.jsonc",
            L"\\..\\Config\\default_config.jsonc",
            L"\\..\\..\\Config\\config.jsonc",
            L"\\..\\..\\Config\\default_config.jsonc"
        };

        for (const wchar_t* rel : relatives) {
            std::wstring candidate = std::wstring(exe_path) + rel;
            wchar_t full_path[MAX_PATH] = { 0 };
            if (GetFullPathNameW(candidate.c_str(), MAX_PATH, full_path, NULL)) {
                if (GetFileAttributesW(full_path) != INVALID_FILE_ATTRIBUTES) {
                    return full_path;
                }
            }
        }
    }

    // Search relative to current working directory
    const wchar_t* cwd_relatives[] = {
        L"Config\\config.jsonc",
        L"Config\\default_config.jsonc",
        L"..\\Config\\config.jsonc",
        L"..\\Config\\default_config.jsonc",
        L"..\\..\\Config\\config.jsonc",
        L"..\\..\\Config\\default_config.jsonc"
    };
    for (const wchar_t* rel : cwd_relatives) {
        wchar_t full_path[MAX_PATH] = { 0 };
        if (GetFullPathNameW(rel, MAX_PATH, full_path, NULL)) {
            if (GetFileAttributesW(full_path) != INVALID_FILE_ATTRIBUTES) {
                return full_path;
            }
        }
    }

    // Fallback: resolve canonical path relative to executable
    if (exe_path[0] != L'\0') {
        std::wstring candidate = std::wstring(exe_path) + L"\\..\\..\\Config\\default_config.jsonc";
        wchar_t full_path[MAX_PATH] = { 0 };
        if (GetFullPathNameW(candidate.c_str(), MAX_PATH, full_path, NULL)) {
            return full_path;
        }
    }

    return L"Config\\default_config.jsonc";
}

cJSON* ConfigIO_Load(const std::wstring& path)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    DWORD file_size = GetFileSize(hFile, NULL);
    if (file_size == INVALID_FILE_SIZE || file_size == 0) {
        CloseHandle(hFile);
        return nullptr;
    }

    std::vector<uint8_t> buffer(file_size + 2, 0);
    DWORD bytes_read = 0;
    if (!ReadFile(hFile, buffer.data(), file_size, &bytes_read, NULL) || bytes_read == 0) {
        CloseHandle(hFile);
        return nullptr;
    }
    CloseHandle(hFile);

    cJSON* root = nullptr;

    // Check for UTF-16LE BOM: 0xFF, 0xFE
    if (bytes_read >= 2 && buffer[0] == 0xFF && buffer[1] == 0xFE) {
        const wchar_t* wstr = reinterpret_cast<const wchar_t*>(buffer.data() + 2);
        int wlen = (bytes_read - 2) / sizeof(wchar_t);
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, NULL, 0, NULL, NULL);
        if (utf8_len > 0) {
            std::string utf8_str(utf8_len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, &utf8_str[0], utf8_len, NULL, NULL);
            if (SUCCEEDED(TE_JsoncParse(utf8_str.c_str(), &root))) {
                return root;
            }
        }
    }

    // Check for UTF-8 BOM: 0xEF, 0xBB, 0xBF
    const char* char_ptr = reinterpret_cast<const char*>(buffer.data());
    if (bytes_read >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF) {
        char_ptr += 3;
    }

    if (SUCCEEDED(TE_JsoncParse(char_ptr, &root))) {
        return root;
    }

    return nullptr;
}

#include <atomic>

static std::atomic<uint64_t> g_config_save_seq{ 0 };

HRESULT ConfigIO_Save(const std::wstring& path, cJSON* root)
{
    if (!root) return E_POINTER;

    char* json_str = cJSON_Print(root);
    if (!json_str) return E_FAIL;

    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    std::wstring dir = separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
    if (!dir.empty()) {
        int create_result = SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
        if (create_result != ERROR_SUCCESS && create_result != ERROR_ALREADY_EXISTS) {
            cJSON_free(json_str);
            return HRESULT_FROM_WIN32((DWORD)create_result);
        }
    }

    uint64_t seq = ++g_config_save_seq;
    std::wstring temp_path = path + L".tmp." 
                           + std::to_wstring(GetCurrentProcessId()) + L"_"
                           + std::to_wstring(GetCurrentThreadId()) + L"_"
                           + std::to_wstring(GetTickCount64()) + L"_"
                           + std::to_wstring(seq);

    std::ofstream out(temp_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        cJSON_free(json_str);
        return E_FAIL;
    }

    out.write(json_str, strlen(json_str));
    bool write_ok = out.good();
    out.close();
    cJSON_free(json_str);

    if (!write_ok) {
        DeleteFileW(temp_path.c_str());
        return E_FAIL;
    }

    if (!MoveFileExW(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
        DWORD error = GetLastError();
        DeleteFileW(temp_path.c_str());
        return HRESULT_FROM_WIN32(error);
    }

    return S_OK;
}

cJSON* ConfigIO_GetPluginValue(cJSON* root, const char* plugin_name, const char* key)
{
    if (!root || !plugin_name || !key) return nullptr;

    cJSON* plugin_section = cJSON_GetObjectItemCaseSensitive(root, "plugins");
    if (!plugin_section || !cJSON_IsObject(plugin_section)) return nullptr;

    cJSON* specific_plugin = cJSON_GetObjectItemCaseSensitive(plugin_section, plugin_name);
    if (!specific_plugin || !cJSON_IsObject(specific_plugin)) return nullptr;

    return cJSON_GetObjectItemCaseSensitive(specific_plugin, key);
}

HRESULT ConfigIO_SetPluginValue(cJSON* root, const char* plugin_name, const char* key, cJSON* value)
{
    if (!root || !plugin_name || !key || !value) {
        if (value) cJSON_Delete(value);
        return E_POINTER;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(value);
        return E_INVALIDARG;
    }

    cJSON* plugin_section = cJSON_GetObjectItemCaseSensitive(root, "plugins");
    if (plugin_section && !cJSON_IsObject(plugin_section)) {
        cJSON_Delete(value);
        return E_INVALIDARG;
    }
    if (!plugin_section) {
        plugin_section = cJSON_CreateObject();
        if (plugin_section && !cJSON_AddItemToObject(root, "plugins", plugin_section)) {
            cJSON_Delete(plugin_section);
            cJSON_Delete(value);
            return E_OUTOFMEMORY;
        }
    }

    if (!plugin_section) {
        cJSON_Delete(value);
        return E_OUTOFMEMORY;
    }

    cJSON* specific_plugin = cJSON_GetObjectItemCaseSensitive(plugin_section, plugin_name);
    if (specific_plugin && !cJSON_IsObject(specific_plugin)) {
        cJSON_Delete(value);
        return E_INVALIDARG;
    }
    if (!specific_plugin) {
        specific_plugin = cJSON_CreateObject();
        if (specific_plugin && !cJSON_AddItemToObject(plugin_section, plugin_name, specific_plugin)) {
            cJSON_Delete(specific_plugin);
            cJSON_Delete(value);
            return E_OUTOFMEMORY;
        }
    }

    if (!specific_plugin) {
        cJSON_Delete(value);
        return E_OUTOFMEMORY;
    }

    // Replace if exists, add if not
    cJSON* existing = cJSON_GetObjectItemCaseSensitive(specific_plugin, key);
    if (existing) {
        if (!cJSON_ReplaceItemInObjectCaseSensitive(specific_plugin, key, value)) {
            cJSON_Delete(value);
            return E_OUTOFMEMORY;
        }
    } else {
        if (!cJSON_AddItemToObject(specific_plugin, key, value)) {
            cJSON_Delete(value);
            return E_OUTOFMEMORY;
        }
    }

    return S_OK;
}

cJSON* ConfigIO_GetWidgetValue(cJSON* root, const char* key)
{
    if (!root || !key) return nullptr;
    cJSON* widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
    if (!widgets || !cJSON_IsObject(widgets)) return nullptr;

    if (strcmp(key, "layout") == 0) {
        cJSON* layout = cJSON_GetObjectItemCaseSensitive(widgets, "layout");
        if (cJSON_IsArray(layout)) return cJSON_Duplicate(layout, 1);
        if (!cJSON_IsString(layout) || !layout->valuestring) return nullptr;
        cJSON* migrated = cJSON_CreateArray();
        if (!migrated) return nullptr;
        std::string text = layout->valuestring;
        size_t begin = 0;
        while (begin < text.size()) {
            const size_t end = text.find(',', begin);
            std::string name = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
            const size_t first = name.find_first_not_of(" \t\r\n");
            const size_t last = name.find_last_not_of(" \t\r\n");
            if (first != std::string::npos) {
                name = name.substr(first, last - first + 1);
                cJSON* entry = cJSON_CreateString(name.c_str());
                if (!entry || !cJSON_AddItemToArray(migrated, entry)) {
                    if (entry) cJSON_Delete(entry);
                    cJSON_Delete(migrated);
                    return nullptr;
                }
            }
            if (end == std::string::npos) break;
            begin = end + 1;
        }
        return migrated;
    }

    const char* dot = strchr(key, '.');
    if (dot) {
        const std::string parent_key(key, static_cast<size_t>(dot - key));
        cJSON* parent = cJSON_GetObjectItemCaseSensitive(widgets, parent_key.c_str());
        return parent && cJSON_IsObject(parent) ? cJSON_GetObjectItemCaseSensitive(parent, dot + 1) : nullptr;
    }
    return cJSON_GetObjectItemCaseSensitive(widgets, key);
}

HRESULT ConfigIO_SetWidgetValue(cJSON* root, const char* key, cJSON* value)
{
    if (!root || !key || !value) { if (value) cJSON_Delete(value); return E_POINTER; }
    cJSON* widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
    if (widgets && !cJSON_IsObject(widgets)) { cJSON_Delete(value); return E_INVALIDARG; }
    if (!widgets) {
        widgets = cJSON_CreateObject();
        if (!widgets || !cJSON_AddItemToObject(root, "widgets", widgets)) {
            if (widgets) cJSON_Delete(widgets);
            cJSON_Delete(value);
            return E_OUTOFMEMORY;
        }
    }

    if (strcmp(key, "layout") == 0 && cJSON_IsString(value)) {
        cJSON* array = cJSON_CreateArray();
        if (!array) { cJSON_Delete(value); return E_OUTOFMEMORY; }
        std::string text = value->valuestring ? value->valuestring : "";
        size_t begin = 0;
        while (begin < text.size()) {
            const size_t end = text.find(',', begin);
            std::string name = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
            const size_t first = name.find_first_not_of(" \t\r\n");
            const size_t last = name.find_last_not_of(" \t\r\n");
            if (first != std::string::npos) {
                name = name.substr(first, last - first + 1);
                cJSON* entry = cJSON_CreateString(name.c_str());
                if (!entry || !cJSON_AddItemToArray(array, entry)) {
                    if (entry) cJSON_Delete(entry);
                    cJSON_Delete(array); cJSON_Delete(value); return E_OUTOFMEMORY;
                }
            }
            if (end == std::string::npos) break;
            begin = end + 1;
        }
        cJSON_Delete(value);
        value = array;
    }
    if (strcmp(key, "layout") == 0 && !cJSON_IsArray(value)) {
        cJSON_Delete(value);
        return E_INVALIDARG;
    }

    cJSON* parent = widgets;
    std::string final_key(key);
    const size_t dot = final_key.find('.');
    if (dot != std::string::npos) {
        const std::string parent_key = final_key.substr(0, dot);
        final_key = final_key.substr(dot + 1);
        parent = cJSON_GetObjectItemCaseSensitive(widgets, parent_key.c_str());
        if (parent && !cJSON_IsObject(parent)) { cJSON_Delete(value); return E_INVALIDARG; }
        if (!parent) {
            parent = cJSON_CreateObject();
            if (!parent || !cJSON_AddItemToObject(widgets, parent_key.c_str(), parent)) {
                if (parent) cJSON_Delete(parent);
                cJSON_Delete(value); return E_OUTOFMEMORY;
            }
        }
    }
    cJSON* existing = cJSON_GetObjectItemCaseSensitive(parent, final_key.c_str());
    if (existing) {
        if (!cJSON_ReplaceItemInObjectCaseSensitive(parent, final_key.c_str(), value)) {
            cJSON_Delete(value); return E_OUTOFMEMORY;
        }
    } else if (!cJSON_AddItemToObject(parent, final_key.c_str(), value)) {
        cJSON_Delete(value); return E_OUTOFMEMORY;
    }
    if (strcmp(key, "layout") == 0) {
        cJSON* version = cJSON_GetObjectItemCaseSensitive(widgets, "schema_version");
        if (!version) cJSON_AddNumberToObject(widgets, "schema_version", 2);
        else if (cJSON_IsNumber(version)) {
            version->valueint = 2;
            version->valuedouble = 2;
        }
    }
    return S_OK;
}
