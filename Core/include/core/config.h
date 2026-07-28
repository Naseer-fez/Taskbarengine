#pragma once

#include <sdk/te_types.h>
#include <cJSON.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resolve absolute path to config.jsonc in LocalAppData directory.
 * @param buf Wide char buffer to receive absolute file path.
 * @param buf_len Length of buffer in wchar_t elements.
 * @return S_OK on success, or E_POINTER / error HRESULT.
 */
HRESULT TE_ConfigResolvePath(wchar_t* buf, size_t buf_len);

/**
 * @brief Load and parse JSONC config file from disk. Creates default config if missing.
 * @param path Wide char file path, or NULL to use default resolved path.
 * @param out_root Pointer to receive parsed cJSON root node.
 * @return S_OK on success, or error HRESULT.
 */
HRESULT TE_ConfigLoad(const wchar_t* path, cJSON** out_root);

/**
 * @brief Extract plugin-specific configuration section from root config.
 * @param root Parsed cJSON root configuration object.
 * @param plugin_name Plugin identifier string.
 * @return Pointer to plugin cJSON sub-object, or NULL if missing/invalid.
 */
const cJSON* TE_ConfigGetPluginSection(const cJSON* root, const char* plugin_name);

/**
 * @brief Extract core engine configuration section from root config.
 * @param root Parsed cJSON root configuration object.
 * @return Pointer to core cJSON sub-object, or NULL if missing.
 */
const cJSON* TE_ConfigGetCoreSection(const cJSON* root);

#ifdef __cplusplus
}
#endif
