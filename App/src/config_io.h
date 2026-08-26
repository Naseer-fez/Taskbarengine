#pragma once

#include <string>
#include <sdk/te_types.h>
#include <cJSON.h>

/**
 * @brief Get the path to the configuration file.
 * @return The absolute path to config.jsonc.
 */
std::wstring ConfigIO_GetConfigPath();

/**
 * @brief Load the configuration file from disk.
 * @param path The path to the configuration file.
 * @return The parsed JSON root object, or nullptr on failure.
 */
cJSON* ConfigIO_Load(const std::wstring& path);

/**
 * @brief Save the configuration file to disk.
 * @param path The path to the configuration file.
 * @param root The JSON root object to save.
 * @return S_OK on success, or an error HRESULT.
 */
HRESULT ConfigIO_Save(const std::wstring& path, cJSON* root);

/**
 * @brief Get a specific setting value for a plugin.
 * @param root The JSON root object.
 * @param plugin_name The name of the plugin.
 * @param key The setting key.
 * @return The JSON node for the setting, or nullptr if not found.
 */
cJSON* ConfigIO_GetPluginValue(cJSON* root, const char* plugin_name, const char* key);

/**
 * @brief Set a specific setting value for a plugin.
 * @param root The JSON root object.
 * @param plugin_name The name of the plugin.
 * @param key The setting key.
 * @param value The JSON node to set (takes ownership).
 * @return S_OK on success, or an error HRESULT.
 */
HRESULT ConfigIO_SetPluginValue(cJSON* root, const char* plugin_name, const char* key, cJSON* value);
