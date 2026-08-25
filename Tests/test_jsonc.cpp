#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include <sdk/te_jsonc.h>
#include <cJSON.h>
}

TEST_CASE("JSONC comment stripper", "[jsonc][strip]") {
    SECTION("Strip line comments") {
        const char* input = "{\"key\": \"value\"} // comment";
        char* stripped = TE_JsoncStripComments(input);
        REQUIRE(stripped != nullptr);
        CHECK(std::string(stripped) == "{\"key\": \"value\"} ");
        free(stripped);
    }

    SECTION("Strip block comments") {
        const char* input = "{/* block */\"key\": \"value\"}";
        char* stripped = TE_JsoncStripComments(input);
        REQUIRE(stripped != nullptr);
        CHECK(std::string(stripped) == "{\"key\": \"value\"}");
        free(stripped);
    }

    SECTION("Preserve strings containing comment-like text") {
        const char* input = "{\"url\": \"http://example.com\"}";
        char* stripped = TE_JsoncStripComments(input);
        REQUIRE(stripped != nullptr);
        CHECK(std::string(stripped) == input);
        free(stripped);
    }

    SECTION("Preserve strings containing block comment syntax") {
        const char* input = "{\"note\": \"use /* carefully */\"}";
        char* stripped = TE_JsoncStripComments(input);
        REQUIRE(stripped != nullptr);
        CHECK(std::string(stripped) == input);
        free(stripped);
    }

    SECTION("Handle escaped quotes in strings") {
        const char* input = "{\"msg\": \"say \\\"hello\\\" // world\"}";
        char* stripped = TE_JsoncStripComments(input);
        REQUIRE(stripped != nullptr);
        CHECK(std::string(stripped) == input);
        free(stripped);
    }

    SECTION("Strip nested block comments") {
        const char* input = "/* a /* b */ c */";
        char* stripped = TE_JsoncStripComments(input);
        REQUIRE(stripped != nullptr);
        CHECK(std::string(stripped) == " c */");
        free(stripped);
    }

    SECTION("Multiple line comments") {
        const char* input =
            "// Header comment\n"
            "{\n"
            "  \"name\": \"TaskbarEngine\", // inline comment 1\n"
            "  // Line comment in middle\n"
            "  \"version\": 1 // inline comment 2\n"
            "}\n"
            "// Trailing comment\n";
        char* stripped = TE_JsoncStripComments(input);
        REQUIRE(stripped != nullptr);
        CHECK(strstr(stripped, "//") == nullptr);
        free(stripped);

        cJSON* root = nullptr;
        HRESULT hr = TE_JsoncParse(input, &root);
        CHECK(hr == TE_S_OK);
        REQUIRE(root != nullptr);

        cJSON* name = cJSON_GetObjectItem(root, "name");
        REQUIRE(name != nullptr);
        CHECK(std::string(cJSON_GetStringValue(name)) == "TaskbarEngine");

        cJSON* ver = cJSON_GetObjectItem(root, "version");
        REQUIRE(ver != nullptr);
        CHECK(ver->valueint == 1);

        TE_JsoncFree(root);
    }
}

TEST_CASE("JSONC parser operations", "[jsonc][parse]") {
    SECTION("Valid JSON parse") {
        const char* input = "{\"core\": {\"log_level\": \"info\"}}";
        cJSON* root = nullptr;
        HRESULT hr = TE_JsoncParse(input, &root);
        CHECK(hr == TE_S_OK);
        REQUIRE(root != nullptr);

        cJSON* core = cJSON_GetObjectItem(root, "core");
        REQUIRE(core != nullptr);
        CHECK(cJSON_IsObject(core));

        cJSON* log_level = cJSON_GetObjectItem(core, "log_level");
        REQUIRE(log_level != nullptr);
        CHECK(cJSON_IsString(log_level));
        CHECK(std::string(cJSON_GetStringValue(log_level)) == "info");

        TE_JsoncFree(root);
    }

    SECTION("Malformed JSON parse") {
        const char* input = "{\"key\": }";
        cJSON* root = nullptr;
        HRESULT hr = TE_JsoncParse(input, &root);
        CHECK(hr == TE_E_FAIL);
        CHECK(root == nullptr);
    }

    SECTION("Nested access") {
        const char* input =
            "{\n"
            "  // Plugins configuration section\n"
            "  \"plugins\": {\n"
            "    \"taskbar_resize\": {\n"
            "      \"height\": 48 /* height in pixels */\n"
            "    }\n"
            "  }\n"
            "}";
        cJSON* root = nullptr;
        HRESULT hr = TE_JsoncParse(input, &root);
        CHECK(hr == TE_S_OK);
        REQUIRE(root != nullptr);

        cJSON* plugins = cJSON_GetObjectItem(root, "plugins");
        REQUIRE(plugins != nullptr);
        CHECK(cJSON_IsObject(plugins));

        cJSON* resize = cJSON_GetObjectItem(plugins, "taskbar_resize");
        REQUIRE(resize != nullptr);
        CHECK(cJSON_IsObject(resize));

        cJSON* height = cJSON_GetObjectItem(resize, "height");
        REQUIRE(height != nullptr);
        CHECK(cJSON_IsNumber(height));
        CHECK(height->valueint == 48);

        TE_JsoncFree(root);
    }

    SECTION("Null input") {
        cJSON* root = nullptr;
        HRESULT hr = TE_JsoncParse(nullptr, &root);
        CHECK(hr == TE_E_INVALIDARG);
        CHECK(root == nullptr);

        CHECK(TE_JsoncStripComments(nullptr) == nullptr);
    }

    SECTION("Null output") {
        HRESULT hr = TE_JsoncParse("{}", nullptr);
        CHECK(hr == TE_E_INVALIDARG);
    }

    SECTION("Empty input") {
        cJSON* root = nullptr;
        HRESULT hr = TE_JsoncParse("", &root);
        CHECK(hr == TE_E_FAIL);
        CHECK(root == nullptr);
    }
}

TEST_CASE("JSONC memory management", "[jsonc][memory]") {
    SECTION("Free NULL") {
        TE_JsoncFree(nullptr);
        SUCCEED("TE_JsoncFree(nullptr) did not crash");
    }
}

TEST_CASE("JSONC default configuration validation", "[jsonc][config]") {
    SECTION("Parse full default config structure") {
        const char* default_config_text =
            "// TaskbarEngine Default Configuration\n"
            "// Edit this file to customize your taskbar.\n"
            "// Changes are detected automatically — no restart required.\n"
            "\n"
            "{\n"
            "    // Core engine settings\n"
            "    \"core\": {\n"
            "        \"log_level\": \"info\",      // debug, info, warning, error\n"
            "        \"log_max_files\": 5,\n"
            "        \"log_max_size_mb\": 5\n"
            "    },\n"
            "\n"
            "    // Plugin configurations\n"
            "    \"plugins\": {\n"
            "        // Taskbar resize plugin — adjusts taskbar height and spacing\n"
            "        \"taskbar_resize\": {\n"
            "            \"enabled\": true,\n"
            "            \"height\": 48,\n"
            "            \"padding_top\": 0,\n"
            "            \"padding_bottom\": 0,\n"
            "            \"icon_spacing\": 4\n"
            "        },\n"
            "\n"
            "        // Icon hover plugin — macOS Dock-style magnification\n"
            "        \"icon_hover\": {\n"
            "            \"enabled\": true,\n"
            "            \"max_scale\": 1.2,\n"
            "            \"radius\": 150,\n"
            "            \"curve\": \"gaussian\",     // gaussian, cubic, cosine, linear\n"
            "            \"speed_ms\": 150\n"
            "        }\n"
            "    }\n"
            "}\n";

        cJSON* root = nullptr;
        HRESULT hr = TE_JsoncParse(default_config_text, &root);
        CHECK(hr == TE_S_OK);
        REQUIRE(root != nullptr);

        cJSON* core = cJSON_GetObjectItem(root, "core");
        REQUIRE(core != nullptr);
        cJSON* log_level = cJSON_GetObjectItem(core, "log_level");
        REQUIRE(log_level != nullptr);
        CHECK(std::string(cJSON_GetStringValue(log_level)) == "info");

        cJSON* plugins = cJSON_GetObjectItem(root, "plugins");
        REQUIRE(plugins != nullptr);

        cJSON* resize = cJSON_GetObjectItem(plugins, "taskbar_resize");
        REQUIRE(resize != nullptr);
        cJSON* resize_enabled = cJSON_GetObjectItem(resize, "enabled");
        REQUIRE(resize_enabled != nullptr);
        CHECK(cJSON_IsTrue(resize_enabled));

        cJSON* hover = cJSON_GetObjectItem(plugins, "icon_hover");
        REQUIRE(hover != nullptr);
        cJSON* curve = cJSON_GetObjectItem(hover, "curve");
        REQUIRE(curve != nullptr);
        CHECK(std::string(cJSON_GetStringValue(curve)) == "gaussian");

        TE_JsoncFree(root);
    }
}
