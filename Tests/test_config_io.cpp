#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <windows.h>
#include <string>
#include <fstream>
#include "../App/src/config_io.h"

TEST_CASE("ConfigIO target and roundtrip", "[config_io]") {
    SECTION("GetConfigPath targets default_config.jsonc") {
        std::wstring path = ConfigIO_GetConfigPath();
        REQUIRE(!path.empty());
        
        // Path should end in default_config.jsonc
        std::wstring target_filename = L"default_config.jsonc";
        REQUIRE(path.size() >= target_filename.size());
        std::wstring ending = path.substr(path.size() - target_filename.size());
        CHECK(_wcsicmp(ending.c_str(), target_filename.c_str()) == 0);

        // File should exist on disk
        DWORD attr = GetFileAttributesW(path.c_str());
        CHECK(attr != INVALID_FILE_ATTRIBUTES);
    }

    SECTION("Load default_config.jsonc and read icon_hover values") {
        std::wstring path = ConfigIO_GetConfigPath();
        cJSON* root = ConfigIO_Load(path);
        REQUIRE(root != nullptr);

        // Verify icon_hover section exists
        cJSON* max_scale = ConfigIO_GetPluginValue(root, "icon_hover", "max_scale");
        REQUIRE(max_scale != nullptr);
        CHECK(max_scale->valuedouble >= 1.0);

        cJSON* radius = ConfigIO_GetPluginValue(root, "icon_hover", "radius");
        REQUIRE(radius != nullptr);
        CHECK(radius->valueint >= 40);

        cJSON* curve = ConfigIO_GetPluginValue(root, "icon_hover", "curve");
        REQUIRE(curve != nullptr);
        REQUIRE(curve->valuestring != nullptr);
        CHECK(std::string(curve->valuestring).length() > 0);

        cJSON* speed_ms = ConfigIO_GetPluginValue(root, "icon_hover", "speed_ms");
        REQUIRE(speed_ms != nullptr);
        CHECK(speed_ms->valueint >= 50);

        // Verify core section is preserved
        cJSON* core = cJSON_GetObjectItemCaseSensitive(root, "core");
        CHECK(core != nullptr);

        cJSON_Delete(root);
    }

    SECTION("SetPluginValue and Save preserves structure atomically") {
        wchar_t temp_dir[MAX_PATH];
        GetTempPathW(MAX_PATH, temp_dir);
        std::wstring test_file = std::wstring(temp_dir) + L"te_test_config_" + std::to_wstring(GetCurrentProcessId()) + L".jsonc";

        // Create an initial config
        const char* initial_json = 
            "{\n"
            "  \"core\": {\"log_level\": \"debug\"},\n"
            "  \"plugins\": {\n"
            "    \"icon_hover\": {\n"
            "      \"enabled\": true,\n"
            "      \"max_scale\": 1.5,\n"
            "      \"radius\": 200,\n"
            "      \"curve\": \"gaussian\",\n"
            "      \"speed_ms\": 150\n"
            "    }\n"
            "  }\n"
            "}\n";
        
        {
            std::ofstream out(test_file.c_str(), std::ios::binary);
            out.write(initial_json, strlen(initial_json));
        }

        cJSON* root = ConfigIO_Load(test_file);
        REQUIRE(root != nullptr);

        // Modify max_scale to 4.5
        HRESULT hr1 = ConfigIO_SetPluginValue(root, "icon_hover", "max_scale", cJSON_CreateNumber(4.5));
        REQUIRE(SUCCEEDED(hr1));

        // Modify curve to cosine
        HRESULT hr2 = ConfigIO_SetPluginValue(root, "icon_hover", "curve", cJSON_CreateString("cosine"));
        REQUIRE(SUCCEEDED(hr2));

        // Save atomically
        HRESULT hrSave = ConfigIO_Save(test_file, root);
        REQUIRE(SUCCEEDED(hrSave));
        cJSON_Delete(root);

        // Re-read and verify changes
        cJSON* reloaded = ConfigIO_Load(test_file);
        REQUIRE(reloaded != nullptr);

        cJSON* scale_node = ConfigIO_GetPluginValue(reloaded, "icon_hover", "max_scale");
        REQUIRE(scale_node != nullptr);
        CHECK(scale_node->valuedouble == Catch::Approx(4.5));

        cJSON* curve_node = ConfigIO_GetPluginValue(reloaded, "icon_hover", "curve");
        REQUIRE(curve_node != nullptr);
        CHECK(std::string(curve_node->valuestring) == "cosine");

        // Verify other keys and sections remained intact
        cJSON* radius_node = ConfigIO_GetPluginValue(reloaded, "icon_hover", "radius");
        REQUIRE(radius_node != nullptr);
        CHECK(radius_node->valueint == 200);

        cJSON* core_node = cJSON_GetObjectItemCaseSensitive(reloaded, "core");
        REQUIRE(core_node != nullptr);

        cJSON_Delete(reloaded);
        DeleteFileW(test_file.c_str());
    }

    SECTION("Load handles UTF-8 BOM correctly") {
        wchar_t temp_dir[MAX_PATH];
        GetTempPathW(MAX_PATH, temp_dir);
        std::wstring test_file = std::wstring(temp_dir) + L"te_test_bom_" + std::to_wstring(GetCurrentProcessId()) + L".jsonc";

        const char bom[] = { '\xEF', '\xBB', '\xBF' };
        const char content[] = "{\"plugins\":{\"icon_hover\":{\"max_scale\":2.0}}}";

        {
            std::ofstream out(test_file.c_str(), std::ios::binary);
            out.write(bom, 3);
            out.write(content, strlen(content));
        }

        cJSON* root = ConfigIO_Load(test_file);
        REQUIRE(root != nullptr);

        cJSON* scale = ConfigIO_GetPluginValue(root, "icon_hover", "max_scale");
        REQUIRE(scale != nullptr);
        CHECK(scale->valuedouble == Catch::Approx(2.0));

        cJSON_Delete(root);
        DeleteFileW(test_file.c_str());
    }
}
