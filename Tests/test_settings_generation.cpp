#include <catch2/catch_test_macros.hpp>
#include <sdk/te_plugin.h>
#include <sdk/te_jsonc.h>
#include <cJSON.h>
#include <string>
#include <vector>

TEST_CASE("Settings Schema Serialization and Parsing for GUI Controls", "[gui][settings]") {
    // Construct a synthetic JSON schema representing all 6 setting types
    const char* sample_schema_json = R"json({
        "plugins": [
            {
                "name": "mock_full_plugin",
                "version": "1.0.0",
                "description": "Mock plugin exposing all control types",
                "settings": [
                    {
                        "key": "enable_feature",
                        "label": "Enable Feature",
                        "tooltip": "Toggle switch for feature",
                        "type": "bool",
                        "default": true
                    },
                    {
                        "key": "pixel_height",
                        "label": "Height",
                        "tooltip": "Numeric integer input",
                        "type": "int",
                        "default": 48,
                        "min": 24,
                        "max": 100,
                        "step": 2
                    },
                    {
                        "key": "scale_factor",
                        "label": "Scale Factor",
                        "tooltip": "Float slider",
                        "type": "float",
                        "default": 1.35,
                        "min": 1.0,
                        "max": 2.5,
                        "step": 0.05
                    },
                    {
                        "key": "custom_name",
                        "label": "Custom Name",
                        "tooltip": "Text box entry",
                        "type": "string",
                        "default": "Taskbar1"
                    },
                    {
                        "key": "animation_easing",
                        "label": "Easing Curve",
                        "tooltip": "Combo box drop down",
                        "type": "enum",
                        "default": "gaussian",
                        "options": ["linear", "cubic", "gaussian", "cosine"]
                    },
                    {
                        "key": "accent_color",
                        "label": "Accent Color",
                        "tooltip": "Color picker input",
                        "type": "color",
                        "default": 4294901760
                    }
                ]
            }
        ]
    })json";

    cJSON* root = cJSON_Parse(sample_schema_json);
    REQUIRE(root != nullptr);

    cJSON* plugins = cJSON_GetObjectItemCaseSensitive(root, "plugins");
    REQUIRE(plugins != nullptr);
    REQUIRE(cJSON_IsArray(plugins));
    REQUIRE(cJSON_GetArraySize(plugins) == 1);

    cJSON* plugin = cJSON_GetArrayItem(plugins, 0);
    REQUIRE(plugin != nullptr);

    cJSON* name = cJSON_GetObjectItemCaseSensitive(plugin, "name");
    REQUIRE(name != nullptr);
    REQUIRE(std::string(name->valuestring) == "mock_full_plugin");

    cJSON* settings = cJSON_GetObjectItemCaseSensitive(plugin, "settings");
    REQUIRE(settings != nullptr);
    REQUIRE(cJSON_IsArray(settings));
    REQUIRE(cJSON_GetArraySize(settings) == 6);

    // 1. Validate Bool mapping (ToggleSwitch)
    cJSON* s_bool = cJSON_GetArrayItem(settings, 0);
    REQUIRE(std::string(cJSON_GetObjectItem(s_bool, "type")->valuestring) == "bool");
    REQUIRE(cJSON_IsTrue(cJSON_GetObjectItem(s_bool, "default")));

    // 2. Validate Int mapping (NumberBox)
    cJSON* s_int = cJSON_GetArrayItem(settings, 1);
    REQUIRE(std::string(cJSON_GetObjectItem(s_int, "type")->valuestring) == "int");
    REQUIRE(cJSON_GetObjectItem(s_int, "default")->valueint == 48);
    REQUIRE(cJSON_GetObjectItem(s_int, "min")->valueint == 24);
    REQUIRE(cJSON_GetObjectItem(s_int, "max")->valueint == 100);
    REQUIRE(cJSON_GetObjectItem(s_int, "step")->valueint == 2);

    // 3. Validate Float mapping (Slider)
    cJSON* s_float = cJSON_GetArrayItem(settings, 2);
    REQUIRE(std::string(cJSON_GetObjectItem(s_float, "type")->valuestring) == "float");
    REQUIRE(cJSON_GetObjectItem(s_float, "default")->valuedouble == 1.35);
    REQUIRE(cJSON_GetObjectItem(s_float, "min")->valuedouble == 1.0);
    REQUIRE(cJSON_GetObjectItem(s_float, "max")->valuedouble == 2.5);

    // 4. Validate String mapping (TextBox)
    cJSON* s_string = cJSON_GetArrayItem(settings, 3);
    REQUIRE(std::string(cJSON_GetObjectItem(s_string, "type")->valuestring) == "string");
    REQUIRE(std::string(cJSON_GetObjectItem(s_string, "default")->valuestring) == "Taskbar1");

    // 5. Validate Enum mapping (ComboBox)
    cJSON* s_enum = cJSON_GetArrayItem(settings, 4);
    REQUIRE(std::string(cJSON_GetObjectItem(s_enum, "type")->valuestring) == "enum");
    REQUIRE(std::string(cJSON_GetObjectItem(s_enum, "default")->valuestring) == "gaussian");
    cJSON* options = cJSON_GetObjectItem(s_enum, "options");
    REQUIRE(options != nullptr);
    REQUIRE(cJSON_IsArray(options));
    REQUIRE(cJSON_GetArraySize(options) == 4);
    REQUIRE(std::string(cJSON_GetArrayItem(options, 0)->valuestring) == "linear");
    REQUIRE(std::string(cJSON_GetArrayItem(options, 2)->valuestring) == "gaussian");

    // 6. Validate Color mapping (ColorPicker)
    cJSON* s_color = cJSON_GetArrayItem(settings, 5);
    REQUIRE(std::string(cJSON_GetObjectItem(s_color, "type")->valuestring) == "color");
    REQUIRE(cJSON_GetObjectItem(s_color, "default")->valuedouble == 4294901760.0);

    cJSON_Delete(root);
}

TEST_CASE("Settings Descriptor Struct Translation Integrity", "[gui][descriptors]") {
    const char* curve_options[] = { "gaussian", "cubic", "linear", "cosine" };
    
    SettingDescriptor d0 = {};
    d0.key = "scale";
    d0.label = "Hover Scale";
    d0.tooltip = "Magnification scale factor";
    d0.type = TE_SETTING_FLOAT;
    d0.value.f.default_val = 1.35f;
    d0.value.f.min = 1.0f;
    d0.value.f.max = 2.0f;
    d0.value.f.step = 0.05f;

    SettingDescriptor d1 = {};
    d1.key = "radius";
    d1.label = "Effect Radius";
    d1.tooltip = "Radius of neighbor icon scaling";
    d1.type = TE_SETTING_INT;
    d1.value.i.default_val = 120;
    d1.value.i.min = 40;
    d1.value.i.max = 300;
    d1.value.i.step = 10;

    SettingDescriptor d2 = {};
    d2.key = "curve";
    d2.label = "Easing Function";
    d2.tooltip = "Interpolation curve";
    d2.type = TE_SETTING_ENUM;
    d2.value.e.default_val = "gaussian";
    d2.value.e.options = curve_options;
    d2.value.e.count = 4;

    SettingDescriptor d3 = {};
    d3.key = "show_border";
    d3.label = "Show Border";
    d3.tooltip = "Highlight active icon";
    d3.type = TE_SETTING_BOOL;
    d3.value.b.default_val = false;

    SettingDescriptor descriptors[] = { d0, d1, d2, d3 };

    PluginSettings settings = {
        descriptors,
        sizeof(descriptors) / sizeof(descriptors[0])
    };

    REQUIRE(settings.count == 4);
    REQUIRE(settings.descriptors[0].type == TE_SETTING_FLOAT);
    REQUIRE(settings.descriptors[0].value.f.default_val == 1.35f);
    REQUIRE(settings.descriptors[1].type == TE_SETTING_INT);
    REQUIRE(settings.descriptors[1].value.i.max == 300);
    REQUIRE(settings.descriptors[2].type == TE_SETTING_ENUM);
    REQUIRE(settings.descriptors[2].value.e.count == 4);
    REQUIRE(std::string(settings.descriptors[2].value.e.options[0]) == "gaussian");
    REQUIRE(settings.descriptors[3].type == TE_SETTING_BOOL);
    REQUIRE(settings.descriptors[3].value.b.default_val == false);
}
