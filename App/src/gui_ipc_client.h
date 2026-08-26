#pragma once

#include <string>
#include <optional>
#include <windows.h>
#include <sdk/te_types.h>

/**
 * @brief Check if the IPC connection to the engine is active.
 * @return true if connected, false otherwise.
 */
bool GuiIpcIsConnected();

/**
 * @brief Request the engine to reload the configuration from disk.
 * @return S_OK on success, or an error HRESULT.
 */
HRESULT GuiIpcReloadConfig();

/**
 * @brief Fetch the settings schema from the engine.
 * @return The JSON schema string, or nullopt on failure.
 */
std::optional<std::string> GuiIpcGetSettings();

/**
 * @brief Fetch the latest performance statistics from the engine.
 * @return The JSON stats string, or nullopt on failure.
 */
std::optional<std::string> GuiIpcGetPerfStats();

/**
 * @brief Fetch the list of loaded plugins from the engine.
 * @return The JSON plugin list string, or nullopt on failure.
 */
std::optional<std::string> GuiIpcGetPluginList();
