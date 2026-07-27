#pragma once

#include "te_types.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse a JSONC file (JSON with single-line comments) into cJSON structure.
 * @param path Wide-string absolute path to the JSONC file.
 * @param out_root Pointer to receive the parsed cJSON root node.
 * @return S_OK on success, error HRESULT on failure.
 * @note Thread safety: Thread-safe.
 */
HRESULT TE_JsoncParse(const wchar_t* path, cJSON** out_root);

/**
 * @brief Parse a JSONC memory string into a cJSON structure.
 * @param json_str Buffer containing JSONC text.
 * @param out_root Pointer to receive the parsed cJSON root node.
 * @return S_OK on success, error HRESULT on failure.
 * @note Thread safety: Thread-safe.
 */
HRESULT TE_JsoncParseString(const char* json_str, cJSON** out_root);

/**
 * @brief Free a cJSON structure.
 * @param root The cJSON root node to free.
 * @note Thread safety: Thread-safe.
 */
void TE_JsoncFree(cJSON* root);

/**
 * @brief Get a plugin's configuration sub-object from root config.
 * @param root Root cJSON configuration object.
 * @param name Plugin name string.
 * @param out_plugin Pointer to receive the plugin cJSON sub-object.
 * @return S_OK on success, HRESULT_FROM_WIN32(ERROR_NOT_FOUND) if key missing.
 * @note Thread safety: Thread-safe for read operations on root.
 */
HRESULT TE_JsoncGetPlugin(const cJSON* root, const char* name, const cJSON** out_plugin);

#ifdef __cplusplus
}
#endif
