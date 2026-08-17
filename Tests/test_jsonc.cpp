#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <sdk/te_jsonc.h>
}

TEST_CASE("JSONC comment stripping removes // comments", "[jsonc]") {
    const char* jsonc_data = R"({
        // This is a comment
        "version": 1,
        "name": "TaskbarEngine" // trailing comment
    })";

    cJSON* root = NULL;
    HRESULT hr = TE_JsoncParseString(jsonc_data, &root);

    REQUIRE(SUCCEEDED(hr));
    REQUIRE(root != nullptr);

    cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    REQUIRE(version != nullptr);
    REQUIRE(cJSON_IsNumber(version));
    REQUIRE(version->valueint == 1);

    cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
    REQUIRE(name != nullptr);
    REQUIRE(cJSON_IsString(name));
    REQUIRE(std::string(name->valuestring) == "TaskbarEngine");

    TE_JsoncFree(root);
}

TEST_CASE("JSONC preserves // inside quoted strings", "[jsonc]") {
    const char* jsonc_data = R"({
        "url": "http://example.com/api//v1",
        "comment": "//not_a_comment"
    })";

    cJSON* root = nullptr;
    HRESULT hr = TE_JsoncParseString(jsonc_data, &root);

    REQUIRE(SUCCEEDED(hr));
    REQUIRE(root != nullptr);

    cJSON* url = cJSON_GetObjectItemCaseSensitive(root, "url");
    REQUIRE(url != nullptr);
    REQUIRE(cJSON_IsString(url));
    REQUIRE(std::string(url->valuestring) == "http://example.com/api//v1");

    cJSON* comment = cJSON_GetObjectItemCaseSensitive(root, "comment");
    REQUIRE(comment != nullptr);
    REQUIRE(cJSON_IsString(comment));
    REQUIRE(std::string(comment->valuestring) == "//not_a_comment");

    TE_JsoncFree(root);
}

TEST_CASE("JSONC returns error for malformed input", "[jsonc]") {
    const char* malformed_jsonc = R"({
        "version": 1,
        "unclosed_string": "oops
    })";

    cJSON* root = nullptr;
    HRESULT hr = TE_JsoncParseString(malformed_jsonc, &root);

    REQUIRE(FAILED(hr));
    REQUIRE(root == nullptr);
}

TEST_CASE("JSONC handles escaped quotes correctly", "[jsonc]") {
    const char* jsonc_data = R"({
        "message": "hello \"world\"",
        "nested": "\"quotes\" inside // not a comment"
    })";

    cJSON* root = nullptr;
    HRESULT hr = TE_JsoncParseString(jsonc_data, &root);

    REQUIRE(SUCCEEDED(hr));
    REQUIRE(root != nullptr);

    cJSON* message = cJSON_GetObjectItemCaseSensitive(root, "message");
    REQUIRE(message != nullptr);
    REQUIRE(cJSON_IsString(message));
    REQUIRE(std::string(message->valuestring) == "hello \"world\"");

    cJSON* nested = cJSON_GetObjectItemCaseSensitive(root, "nested");
    REQUIRE(nested != nullptr);
    REQUIRE(cJSON_IsString(nested));
    REQUIRE(std::string(nested->valuestring) == "\"quotes\" inside // not a comment");

    TE_JsoncFree(root);
}

TEST_CASE("JSONC handles escaped backslashes correctly", "[jsonc]") {
    const char* jsonc_data = R"({
        "path": "C:\\Windows\\System32",
        "trailing": "path\\\\" // Should correctly match the backslashes and not escape the quote
    })";

    cJSON* root = nullptr;
    HRESULT hr = TE_JsoncParseString(jsonc_data, &root);

    REQUIRE(SUCCEEDED(hr));
    REQUIRE(root != nullptr);

    cJSON* path = cJSON_GetObjectItemCaseSensitive(root, "path");
    REQUIRE(path != nullptr);
    REQUIRE(cJSON_IsString(path));
    REQUIRE(std::string(path->valuestring) == "C:\\Windows\\System32");

    cJSON* trailing = cJSON_GetObjectItemCaseSensitive(root, "trailing");
    REQUIRE(trailing != nullptr);
    REQUIRE(cJSON_IsString(trailing));
    REQUIRE(std::string(trailing->valuestring) == "path\\\\");

    TE_JsoncFree(root);
}

TEST_CASE("JSONC handles empty strings correctly", "[jsonc]") {
    const char* jsonc_data = R"({
        "empty": "",
        "comment": "" // comment after empty string
    })";

    cJSON* root = nullptr;
    HRESULT hr = TE_JsoncParseString(jsonc_data, &root);

    REQUIRE(SUCCEEDED(hr));
    REQUIRE(root != nullptr);

    cJSON* empty = cJSON_GetObjectItemCaseSensitive(root, "empty");
    REQUIRE(empty != nullptr);
    REQUIRE(cJSON_IsString(empty));
    REQUIRE(std::string(empty->valuestring) == "");

    TE_JsoncFree(root);
}
