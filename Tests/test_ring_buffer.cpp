#include <catch2/catch_test_macros.hpp>
#include <sdk/te_log_impl.h>
#include <sdk/te_log.h>
#include <windows.h>

TEST_CASE("Ring Buffer Logger tests", "[log]") {
    SECTION("Struct size check") {
        REQUIRE(sizeof(TE_LogEntry) == 256);
    }

    SECTION("Log Init and Shutdown") {
        wchar_t temp_path[MAX_PATH];
        GetTempPathW(MAX_PATH, temp_path);
        wcscat(temp_path, L"TE_LogTestDir");

        HRESULT hr = TE_LogInit(temp_path, TE_LOG_DEBUG, true);
        REQUIRE(SUCCEEDED(hr));

        TE_LogWrite(TE_LOG_INFO, "Test log message %d", 123);
        TE_LogWrite(TE_LOG_WARN, "Warning log message %s", "test");

        TE_LogShutdown();
    }
}
