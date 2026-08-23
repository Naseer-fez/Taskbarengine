#include <catch2/catch_test_macros.hpp>
#include <core/config.h>
#include <sdk/te_jsonc.h>

extern "C" bool TE_CoreManagerIsPluginEnabledInConfig(const cJSON* config);
extern "C" bool TE_CoreManagerIsPluginEnabledInConfig(const cJSON* config) {
    if (!config) return false;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(config, "enabled");
    if (!item || !cJSON_IsBool(item)) return false;
    return cJSON_IsTrue(item);
}

TEST_CASE("Config Parsing and Section Retrieval", "[config]") {
    SECTION("Parse config string and extract sections") {
        const char* json_str = R"({
            "version": 1,
            "core": {
                "log_level": "info",
                "log_to_file": true
            },
            "plugin": {
                "DummyPlugin": {
                    "enabled": true,
                    "size": 48
                }
            }
        })";

        cJSON* root = nullptr;
        HRESULT hr_parse = TE_JsoncParseString(json_str, &root);
        REQUIRE(SUCCEEDED(hr_parse));
        REQUIRE(root != nullptr);

        const cJSON* core_sec = TE_ConfigGetCoreSection(root);
        REQUIRE(core_sec != nullptr);
        cJSON* log_lvl = cJSON_GetObjectItemCaseSensitive(core_sec, "log_level");
        REQUIRE(log_lvl != nullptr);
        REQUIRE(std::string(log_lvl->valuestring) == "info");

        const cJSON* dummy_sec = TE_ConfigGetPluginSection(root, "DummyPlugin");
        REQUIRE(dummy_sec != nullptr);
        cJSON* enabled = cJSON_GetObjectItemCaseSensitive(dummy_sec, "enabled");
        REQUIRE(enabled != nullptr);
        REQUIRE(cJSON_IsTrue(enabled));

        const cJSON* missing_sec = TE_ConfigGetPluginSection(root, "NonExistentPlugin");
        REQUIRE(missing_sec == nullptr);

        cJSON_Delete(root);
    }

    SECTION("Parse config with missing fields and unknown sections") {
        const char* json_str = R"({
            "plugin": {
                "KnownPlugin": {
                    "size": 32
                },
                "UnknownPlugin": {
                    "foo": "bar"
                }
            }
        })";

        cJSON* root = nullptr;
        HRESULT hr_parse = TE_JsoncParseString(json_str, &root);
        REQUIRE(SUCCEEDED(hr_parse));
        REQUIRE(root != nullptr);

        /* Core section missing -> returns null */
        const cJSON* core_sec = TE_ConfigGetCoreSection(root);
        REQUIRE(core_sec == nullptr);

        /* Known plugin retrieved */
        const cJSON* known_sec = TE_ConfigGetPluginSection(root, "KnownPlugin");
        REQUIRE(known_sec != nullptr);
        cJSON* enabled = cJSON_GetObjectItemCaseSensitive(known_sec, "enabled");
        REQUIRE(enabled == nullptr); /* Missing field returns null safely */

        /* Unknown plugin section present but safely ignored by core engine lookup */
        const cJSON* unk_sec = TE_ConfigGetPluginSection(root, "UnknownPlugin");
        REQUIRE(unk_sec != nullptr);

        cJSON_Delete(root);
    }

    SECTION("Config path resolution") {
        wchar_t path[MAX_PATH];
        HRESULT hr = TE_ConfigResolvePath(path, MAX_PATH);
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(wcsstr(path, L"config.jsonc") != nullptr);
    }
}
