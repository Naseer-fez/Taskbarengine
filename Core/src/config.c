#include "core/config.h"
#include <string.h>
#include <sdk/te_jsonc.h>
#include <cJSON.h>

HRESULT TE_ConfigLoad(const wchar_t* path, struct cJSON** out_root)
{
    if (!path || !out_root) return TE_E_INVALIDARG;
    return TE_JsoncParseFile(path, out_root);
}

const struct cJSON* TE_ConfigGetPluginSection(const struct cJSON* root, const char* plugin_name)
{
    if (!root || !plugin_name) return NULL;
    
    const struct cJSON* plugins = cJSON_GetObjectItem(root, "plugins");
    if (!plugins) return NULL;
    
    return cJSON_GetObjectItem(plugins, plugin_name);
}

int TE_ConfigGetInt(const struct cJSON* section, const char* key, int default_val)
{
    if (!section || !key) return default_val;
    
    const struct cJSON* item = cJSON_GetObjectItem(section, key);
    if (item && cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return default_val;
}

float TE_ConfigGetFloat(const struct cJSON* section, const char* key, float default_val)
{
    if (!section || !key) return default_val;
    
    const struct cJSON* item = cJSON_GetObjectItem(section, key);
    if (item && cJSON_IsNumber(item)) {
        return (float)item->valuedouble;
    }
    return default_val;
}

BOOL TE_ConfigGetBool(const struct cJSON* section, const char* key, BOOL default_val)
{
    if (!section || !key) return default_val;
    
    const struct cJSON* item = cJSON_GetObjectItem(section, key);
    if (item && cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return default_val;
}

const char* TE_ConfigGetString(const struct cJSON* section, const char* key, const char* default_val)
{
    if (!section || !key) return default_val;
    
    const struct cJSON* item = cJSON_GetObjectItem(section, key);
    if (item && cJSON_IsString(item)) {
        return cJSON_GetStringValue(item);
    }
    return default_val;
}

BOOL TE_ConfigDiffPlugins(const struct cJSON* old_root, const struct cJSON* new_root,
                          const char** changed_names, int* out_count, int max_count)
{
    if (!new_root || !changed_names || !out_count || max_count <= 0) return FALSE;
    
    *out_count = 0;
    BOOL diff_found = FALSE;
    
    const struct cJSON* new_plugins = cJSON_GetObjectItem(new_root, "plugins");
    const struct cJSON* old_plugins = old_root ? cJSON_GetObjectItem(old_root, "plugins") : NULL;
    
    if (!new_plugins) {
        if (old_plugins && old_plugins->child) {
            // All plugins were removed
            struct cJSON* old_child = old_plugins->child;
            while (old_child && *out_count < max_count) {
                if (old_child->string) {
                    changed_names[*out_count] = old_child->string;
                    (*out_count)++;
                    diff_found = TRUE;
                }
                old_child = old_child->next;
            }
        }
        return diff_found;
    }

    // Check for new/modified plugins
    struct cJSON* new_child = new_plugins->child;
    while (new_child && *out_count < max_count) {
        if (new_child->string) {
            const struct cJSON* old_child = old_plugins ? cJSON_GetObjectItem(old_plugins, new_child->string) : NULL;
            if (!old_child || !cJSON_Compare(new_child, old_child, cJSON_True)) {
                changed_names[*out_count] = new_child->string;
                (*out_count)++;
                diff_found = TRUE;
            }
        }
        new_child = new_child->next;
    }

    // Check for removed plugins
    if (old_plugins) {
        struct cJSON* old_child = old_plugins->child;
        while (old_child && *out_count < max_count) {
            if (old_child->string) {
                if (!cJSON_GetObjectItem(new_plugins, old_child->string)) {
                    changed_names[*out_count] = old_child->string;
                    (*out_count)++;
                    diff_found = TRUE;
                }
            }
            old_child = old_child->next;
        }
    }

    return diff_found;
}
