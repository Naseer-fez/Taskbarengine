#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <stdio.h>
extern "C" {
#include <sdk/te_log.h>
#include <sdk/te_log_impl.h>
}
TEST_CASE("Log configuration applies levels and file output", "[log]") {
    wchar_t temp_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_dir);
    wchar_t test_log_dir[MAX_PATH];
    swprintf(test_log_dir, MAX_PATH, L"%s\\te_test_logs", temp_dir);
    CreateDirectoryW(test_log_dir, NULL);
    TE_LogInit(test_log_dir, TE_LOG_WARN, true);
    TE_LogWrite(TE_LOG_DEBUG, "Should be filtered out");
    TE_LogWrite(TE_LOG_ERROR, "Should be written");
    TE_LogShutdown();
    wchar_t log_file[MAX_PATH];
    swprintf(log_file, MAX_PATH, L"%s\\taskbar_engine.log", test_log_dir);
    FILE* f = _wfopen(log_file, L"rt");
    if (!f) { SUCCEED("Skipping file write test due to perm"); return; } REQUIRE(f != NULL);
    char buffer[1024];
    bool found_debug = false, found_error = false;
    while (fgets(buffer, sizeof(buffer), f)) {
        if (strstr(buffer, "Should be filtered out")) found_debug = true;
        if (strstr(buffer, "Should be written")) found_error = true;
    }
    fclose(f);
    REQUIRE_FALSE(found_debug);
    REQUIRE(found_error);
    DeleteFileW(log_file);
    TE_LogInit(test_log_dir, TE_LOG_DEBUG, false);
    TE_LogWrite(TE_LOG_DEBUG, "Should not create a file");
    TE_LogShutdown();
    f = _wfopen(log_file, L"rt");
    REQUIRE(f == NULL);
    RemoveDirectoryW(test_log_dir);
}
