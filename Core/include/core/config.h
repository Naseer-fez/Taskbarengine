#pragma once
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;

/**
 * Load and parse a JSONC configuration file.
 * @param path      Wide-char path to the config.jsonc file.
 * @param out_root  Receives the parsed cJSON root. Caller must free via TE_JsoncFree().
 * @return TE_S_OK on success.
 * @note Thread Safety: Thread-safe.
 */
HRESULT TE_ConfigLoad(const wchar_t* path, struct cJSON** out_root);

/**
 * Get the "plugins.<name>" sub-object from a parsed config root.
 * @param root        Parsed config root.
 * @param plugin_name Plugin name key (e.g., "taskbar_resize").
 * @return Pointer to the plugin's cJSON sub-object, or NULL if not found.
 * @note Thread Safety: Thread-safe if root is not being mutated.
 */
const struct cJSON* TE_ConfigGetPluginSection(const struct cJSON* root, const char* plugin_name);

/**
 * Read an integer value from a config section with a default fallback.
 * @note Thread Safety: Thread-safe if section is not being mutated.
 */
int TE_ConfigGetInt(const struct cJSON* section, const char* key, int default_val);

/**
 * Read a float value from a config section with a default fallback.
 * @note Thread Safety: Thread-safe if section is not being mutated.
 */
float TE_ConfigGetFloat(const struct cJSON* section, const char* key, float default_val);

/**
 * Read a boolean value from a config section with a default fallback.
 * @note Thread Safety: Thread-safe if section is not being mutated.
 */
BOOL TE_ConfigGetBool(const struct cJSON* section, const char* key, BOOL default_val);

/**
 * Read a string value from a config section with a default fallback.
 * @return Pointer to internal cJSON string (do NOT free), or default_val if missing.
 * @note Thread Safety: Thread-safe if section is not being mutated.
 */
const char* TE_ConfigGetString(const struct cJSON* section, const char* key, const char* default_val);

/**
 * Compare two config roots and find which plugin sections changed.
 * @param old_root       Previous config root (may be NULL for first load).
 * @param new_root       New config root.
 * @param changed_names  Output array of plugin name strings (points into cJSON internals).
 * @param out_count      Receives the number of changed plugin names.
 * @param max_count      Maximum entries in changed_names array.
 * @return TRUE if any differences found, FALSE otherwise.
 * @note Thread Safety: Thread-safe if both roots are not being mutated.
 */
BOOL TE_ConfigDiffPlugins(const struct cJSON* old_root, const struct cJSON* new_root,
                          const char** changed_names, int* out_count, int max_count);

#ifdef __cplusplus
}
#endif
