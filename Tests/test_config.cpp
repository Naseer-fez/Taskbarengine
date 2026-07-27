#include <catch2/catch_test_macros.hpp>
#include <core/config.h>
#include <sdk/te_jsonc.h>

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

        cJSON* root = TE_JsoncParseString(json_str);
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

    SECTION("Config path resolution") {
        wchar_t path[MAX_PATH];
        HRESULT hr = TE_ConfigResolvePath(path, MAX_PATH);
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(wcsstr(path, L"config.jsonc") != nullptr);
    }
}
