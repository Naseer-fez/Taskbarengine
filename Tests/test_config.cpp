#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>

extern "C" {
#include <sdk/te_jsonc.h>
#include <cJSON.h>
}

/* We test the config functions directly. Since they're in Core (a DLL),
   we re-declare and link them for testing. For unit tests, we include
   the config source directly or link against a test library. */
extern "C" {
#include <sdk/te_types.h>
/* Forward-declare config functions for testing */
HRESULT TE_ConfigLoad(const wchar_t* path, cJSON** out_root);
const cJSON* TE_ConfigGetPluginSection(const cJSON* root, const char* plugin_name);
int TE_ConfigGetInt(const cJSON* section, const char* key, int default_val);
float TE_ConfigGetFloat(const cJSON* section, const char* key, float default_val);
BOOL TE_ConfigGetBool(const cJSON* section, const char* key, BOOL default_val);
const char* TE_ConfigGetString(const cJSON* section, const char* key, const char* default_val);
BOOL TE_ConfigDiffPlugins(const cJSON* old_root, const cJSON* new_root,
                          const char** changed_names, int* out_count, int max_count);
}

static cJSON* ParseTestConfig(const char* json_text) {
    cJSON* root = nullptr;
    TE_JsoncParse(json_text, &root);
    return root;
}

TEST_CASE("Config accessor functions", "[config]") {
    const char* config_text =
        "{\"plugins\": {\"test_plugin\": {"
        "\"height\": 48, \"scale\": 1.5, \"enabled\": true, \"name\": \"hello\""
        "}}}";
    cJSON* root = ParseTestConfig(config_text);
    REQUIRE(root != nullptr);

    SECTION("Get plugin section") {
        const cJSON* section = TE_ConfigGetPluginSection(root, "test_plugin");
        REQUIRE(section != nullptr);

        const cJSON* missing = TE_ConfigGetPluginSection(root, "nonexistent");
        CHECK(missing == nullptr);
    }

    SECTION("Get integer value") {
        const cJSON* section = TE_ConfigGetPluginSection(root, "test_plugin");
        REQUIRE(section != nullptr);
        CHECK(TE_ConfigGetInt(section, "height", 0) == 48);
        CHECK(TE_ConfigGetInt(section, "missing", 99) == 99);
    }

    SECTION("Get float value") {
        const cJSON* section = TE_ConfigGetPluginSection(root, "test_plugin");
        REQUIRE(section != nullptr);
        CHECK(TE_ConfigGetFloat(section, "scale", 0.0f) == Catch::Approx(1.5f));
        CHECK(TE_ConfigGetFloat(section, "missing", 2.0f) == Catch::Approx(2.0f));
    }

    SECTION("Get bool value") {
        const cJSON* section = TE_ConfigGetPluginSection(root, "test_plugin");
        REQUIRE(section != nullptr);
        CHECK(TE_ConfigGetBool(section, "enabled", FALSE) == TRUE);
        CHECK(TE_ConfigGetBool(section, "missing", FALSE) == FALSE);
    }

    SECTION("Get string value") {
        const cJSON* section = TE_ConfigGetPluginSection(root, "test_plugin");
        REQUIRE(section != nullptr);
        CHECK(std::string(TE_ConfigGetString(section, "name", "default")) == "hello");
        CHECK(std::string(TE_ConfigGetString(section, "missing", "fallback")) == "fallback");
    }

    SECTION("Null section returns defaults") {
        CHECK(TE_ConfigGetInt(nullptr, "height", 42) == 42);
        CHECK(TE_ConfigGetFloat(nullptr, "scale", 1.0f) == Catch::Approx(1.0f));
        CHECK(TE_ConfigGetBool(nullptr, "enabled", TRUE) == TRUE);
        CHECK(std::string(TE_ConfigGetString(nullptr, "name", "def")) == "def");
    }

    TE_JsoncFree(root);
}

TEST_CASE("Config diff detection", "[config]") {
    SECTION("Detect changed plugin section") {
        cJSON* old_root = ParseTestConfig(
            "{\"plugins\": {\"p1\": {\"val\": 1}, \"p2\": {\"val\": 2}}}");
        cJSON* new_root = ParseTestConfig(
            "{\"plugins\": {\"p1\": {\"val\": 1}, \"p2\": {\"val\": 99}}}");
        REQUIRE(old_root != nullptr);
        REQUIRE(new_root != nullptr);

        const char* changed[8];
        int count = 0;
        BOOL has_diff = TE_ConfigDiffPlugins(old_root, new_root, changed, &count, 8);
        CHECK(has_diff == TRUE);
        CHECK(count == 1);
        CHECK(std::string(changed[0]) == "p2");

        TE_JsoncFree(old_root);
        TE_JsoncFree(new_root);
    }

    SECTION("No changes detected") {
        cJSON* old_root = ParseTestConfig("{\"plugins\": {\"p1\": {\"val\": 1}}}");
        cJSON* new_root = ParseTestConfig("{\"plugins\": {\"p1\": {\"val\": 1}}}");
        REQUIRE(old_root != nullptr);
        REQUIRE(new_root != nullptr);

        const char* changed[8];
        int count = 0;
        BOOL has_diff = TE_ConfigDiffPlugins(old_root, new_root, changed, &count, 8);
        CHECK(has_diff == FALSE);
        CHECK(count == 0);

        TE_JsoncFree(old_root);
        TE_JsoncFree(new_root);
    }

    SECTION("New plugin added") {
        cJSON* old_root = ParseTestConfig("{\"plugins\": {\"p1\": {\"val\": 1}}}");
        cJSON* new_root = ParseTestConfig(
            "{\"plugins\": {\"p1\": {\"val\": 1}, \"p2\": {\"val\": 2}}}");

        const char* changed[8];
        int count = 0;
        BOOL has_diff = TE_ConfigDiffPlugins(old_root, new_root, changed, &count, 8);
        CHECK(has_diff == TRUE);
        CHECK(count == 1);
        CHECK(std::string(changed[0]) == "p2");

        TE_JsoncFree(old_root);
        TE_JsoncFree(new_root);
    }

    SECTION("NULL old root treats everything as changed") {
        cJSON* new_root = ParseTestConfig(
            "{\"plugins\": {\"p1\": {\"val\": 1}, \"p2\": {\"val\": 2}}}");

        const char* changed[8];
        int count = 0;
        BOOL has_diff = TE_ConfigDiffPlugins(nullptr, new_root, changed, &count, 8);
        CHECK(has_diff == TRUE);
        CHECK(count == 2);

        TE_JsoncFree(new_root);
    }
}
