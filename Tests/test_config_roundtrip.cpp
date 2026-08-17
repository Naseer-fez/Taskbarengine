#include <catch2/catch_test_macros.hpp>
#include <core/config.h>
#include <sdk/te_jsonc.h>
#include <windows.h>
#include <fstream>
#include <string>

static void WriteTempConfigFile(const wchar_t* path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
}

TEST_CASE("Config Hot-Reload - Direct Overwrite and Parsing", "[config][roundtrip]") {
    wchar_t temp_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_dir);
    wchar_t temp_file[MAX_PATH];
    swprintf(temp_file, MAX_PATH, L"%sTE_test_config_%lu.jsonc", temp_dir, GetCurrentProcessId());

    std::string v1_jsonc = 
        "// TaskbarEngine Initial Configuration\n"
        "{\n"
        "  \"core\": {\n"
        "    \"log_level\": \"INFO\", // Log comment\n"
        "    \"poll_rate\": 60\n"
        "  },\n"
        "  \"plugin\": {\n"
        "    \"taskbar_resize\": {\n"
        "      \"enabled\": true,\n"
        "      \"height\": 48 // Desired height\n"
        "    }\n"
        "  }\n"
        "}\n";

    WriteTempConfigFile(temp_file, v1_jsonc);

    cJSON* root1 = nullptr;
    HRESULT hr1 = TE_ConfigLoad(temp_file, &root1);
    REQUIRE(SUCCEEDED(hr1));
    REQUIRE(root1 != nullptr);

    const cJSON* resize1 = TE_ConfigGetPluginSection(root1, "taskbar_resize");
    REQUIRE(resize1 != nullptr);
    const cJSON* height1 = cJSON_GetObjectItemCaseSensitive(resize1, "height");
    REQUIRE(height1 != nullptr);
    REQUIRE(height1->valueint == 48);

    /* Overwrite file with new settings */
    std::string v2_jsonc = 
        "{\n"
        "  \"core\": { \"log_level\": \"DEBUG\" },\n"
        "  \"plugin\": {\n"
        "    \"taskbar_resize\": {\n"
        "      \"enabled\": false,\n"
        "      \"height\": 64\n"
        "    }\n"
        "  }\n"
        "}\n";

    WriteTempConfigFile(temp_file, v2_jsonc);

    cJSON* root2 = nullptr;
    HRESULT hr2 = TE_ConfigLoad(temp_file, &root2);
    REQUIRE(SUCCEEDED(hr2));
    REQUIRE(root2 != nullptr);

    const cJSON* resize2 = TE_ConfigGetPluginSection(root2, "taskbar_resize");
    REQUIRE(resize2 != nullptr);
    const cJSON* height2 = cJSON_GetObjectItemCaseSensitive(resize2, "height");
    REQUIRE(height2 != nullptr);
    REQUIRE(height2->valueint == 64);
    const cJSON* en2 = cJSON_GetObjectItemCaseSensitive(resize2, "enabled");
    REQUIRE(en2 != nullptr);
    REQUIRE(cJSON_IsFalse(en2));

    cJSON_Delete(root1);
    cJSON_Delete(root2);
    DeleteFileW(temp_file);
}

TEST_CASE("Config Hot-Reload - Invalid JSON Preserves Stability", "[config][roundtrip]") {
    wchar_t temp_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_dir);
    wchar_t temp_file[MAX_PATH];
    swprintf(temp_file, MAX_PATH, L"%sTE_test_invalid_%lu.jsonc", temp_dir, GetCurrentProcessId());

    std::string malformed = "{ \"core\": { \"log_level\": \"INFO\" ... unclosed ";
    WriteTempConfigFile(temp_file, malformed);

    cJSON* root = nullptr;
    HRESULT hr = TE_ConfigLoad(temp_file, &root);
    REQUIRE(FAILED(hr));
    REQUIRE(root == nullptr);

    DeleteFileW(temp_file);
}
