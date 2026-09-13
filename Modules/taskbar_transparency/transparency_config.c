#include "transparency_config.h"
#include <sdk/te_jsonc.h>
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void TE_TransparencyConfigSetDefaults(TE_TransparencySettings* settings) {
    if (!settings) return;
    settings->enabled = true;
    settings->mode = TE_MODE_CLEAR;
    settings->opacity = 0.0f;
    settings->color_argb = 0;
    settings->accent_flags = 2;
    settings->apply_secondary = true;
}

TE_TransparencyMode TE_TransparencyModeFromString(const char* str) {
    if (!str) return TE_MODE_CLEAR;

    /* Trim leading whitespace */
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
        str++;
    }

    if (_stricmp(str, "clear") == 0 || _stricmp(str, "transparent") == 0) {
        return TE_MODE_CLEAR;
    }
    if (_stricmp(str, "blur") == 0) {
        return TE_MODE_BLUR;
    }
    if (_stricmp(str, "acrylic") == 0) {
        return TE_MODE_ACRYLIC;
    }
    if (_stricmp(str, "tint") == 0 || _stricmp(str, "opaque") == 0) {
        return TE_MODE_TINT;
    }
    if (_stricmp(str, "disabled") == 0 || _stricmp(str, "native") == 0) {
        return TE_MODE_DISABLED;
    }

    return TE_MODE_CLEAR;
}

const char* TE_TransparencyModeToString(TE_TransparencyMode mode) {
    switch (mode) {
        case TE_MODE_CLEAR:
            return "clear";
        case TE_MODE_BLUR:
            return "blur";
        case TE_MODE_ACRYLIC:
            return "acrylic";
        case TE_MODE_TINT:
            return "tint";
        case TE_MODE_DISABLED:
            return "disabled";
        default:
            return "clear";
    }
}

static uint32_t ParseColorValue(const cJSON* item) {
    if (!item) return 0;

    if (cJSON_IsNumber(item)) {
        return (uint32_t)item->valuedouble;
    }

    if (cJSON_IsString(item) && item->valuestring) {
        const char* s = item->valuestring;
        while (*s == ' ' || *s == '\t') s++;

        if (*s == '#') {
            s++;
        } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
        }

        char* endptr = NULL;
        unsigned long val = strtoul(s, &endptr, 16);
        return (uint32_t)val;
    }

    return 0;
}

bool TE_TransparencyConfigParseJson(const struct cJSON* config, TE_TransparencySettings* settings) {
    if (!settings) {
        return false;
    }
    if (!config) {
        return false;
    }

    /* Check if wrapped in "taskbar_transparency" or "plugins" -> "taskbar_transparency" */
    const cJSON* target = config;
    const cJSON* plugins_sec = cJSON_GetObjectItemCaseSensitive(config, "plugins");
    if (plugins_sec && cJSON_IsObject(plugins_sec)) {
        const cJSON* inner = cJSON_GetObjectItemCaseSensitive(plugins_sec, "taskbar_transparency");
        if (inner && cJSON_IsObject(inner)) {
            target = inner;
        }
    } else {
        const cJSON* sec = cJSON_GetObjectItemCaseSensitive(config, "taskbar_transparency");
        if (sec && cJSON_IsObject(sec)) {
            target = sec;
        }
    }

    /* 1. enabled */
    const cJSON* enabled_item = cJSON_GetObjectItemCaseSensitive(target, "enabled");
    if (enabled_item && cJSON_IsBool(enabled_item)) {
        settings->enabled = cJSON_IsTrue(enabled_item) ? true : false;
    }

    /* 2. mode */
    const cJSON* mode_item = cJSON_GetObjectItemCaseSensitive(target, "mode");
    if (mode_item) {
        if (cJSON_IsString(mode_item) && mode_item->valuestring) {
            settings->mode = TE_TransparencyModeFromString(mode_item->valuestring);
        } else if (cJSON_IsNumber(mode_item)) {
            int m = (int)mode_item->valuedouble;
            if (m >= TE_MODE_CLEAR && m <= TE_MODE_DISABLED) {
                settings->mode = (TE_TransparencyMode)m;
            }
        }
    }

    /* 3. opacity */
    const cJSON* opacity_item = cJSON_GetObjectItemCaseSensitive(target, "opacity");
    if (opacity_item && cJSON_IsNumber(opacity_item)) {
        float op = (float)opacity_item->valuedouble;
        if (op < 0.0f) op = 0.0f;
        if (op > 1.0f) op = 1.0f;
        settings->opacity = op;
    }

    /* 4. color / color_argb */
    const cJSON* color_item = cJSON_GetObjectItemCaseSensitive(target, "color");
    if (!color_item) {
        color_item = cJSON_GetObjectItemCaseSensitive(target, "color_argb");
    }
    if (color_item) {
        settings->color_argb = ParseColorValue(color_item);
    }

    /* 5. accent_flags */
    const cJSON* flags_item = cJSON_GetObjectItemCaseSensitive(target, "accent_flags");
    if (flags_item && cJSON_IsNumber(flags_item)) {
        settings->accent_flags = (uint32_t)flags_item->valuedouble;
    }

    /* 6. apply_secondary */
    const cJSON* sec_item = cJSON_GetObjectItemCaseSensitive(target, "apply_secondary");
    if (sec_item && cJSON_IsBool(sec_item)) {
        settings->apply_secondary = cJSON_IsTrue(sec_item) ? true : false;
    }

    return true;
}

bool TE_TransparencyConfigParse(const char* json_str, TE_TransparencySettings* settings) {
    if (!json_str || !settings) {
        return false;
    }

    TE_TransparencyConfigSetDefaults(settings);

    cJSON* root = NULL;
    HRESULT hr = TE_JsoncParse(json_str, &root);
    if (TE_SUCCEEDED(hr) && root) {
        bool ok = TE_TransparencyConfigParseJson(root, settings);
        TE_JsoncFree(root);
        return ok;
    }

    /* Fallback directly to cJSON_Parse if TE_JsoncParse did not succeed */
    root = cJSON_Parse(json_str);
    if (root) {
        bool ok = TE_TransparencyConfigParseJson(root, settings);
        cJSON_Delete(root);
        return ok;
    }

    return false;
}
