#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "composition_api.h"
#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;

/**
 * Initializes settings with documented default values.
 */
void TE_TransparencyConfigSetDefaults(TE_TransparencySettings* settings);

/**
 * Parses JSON / JSONC string into TE_TransparencySettings.
 */
bool TE_TransparencyConfigParse(const char* json_str, TE_TransparencySettings* settings);

/**
 * Parses a cJSON object into TE_TransparencySettings.
 */
bool TE_TransparencyConfigParseJson(const struct cJSON* config, TE_TransparencySettings* settings);

/**
 * Parses string representation of visual mode to enum.
 */
TE_TransparencyMode TE_TransparencyModeFromString(const char* str);

/**
 * Formats visual mode enum to string representation.
 */
const char* TE_TransparencyModeToString(TE_TransparencyMode mode);

#ifdef __cplusplus
}
#endif
