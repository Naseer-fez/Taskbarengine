#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include <filesystem>

extern "C" {
#include <sdk/te_types.h>
#include <sdk/te_log.h>
#include <sdk/te_log_impl.h>
}

TEST_CASE("Ring buffer logger init and shutdown", "[logging]") {
    /* Use a temp directory for log output */
    wchar_t temp_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_dir);
    wcscat_s(temp_dir, MAX_PATH, L"te_test_logs");
    CreateDirectoryW(temp_dir, NULL);

    SECTION("Init and shutdown without crash") {
        HRESULT hr = TE_LogInit(temp_dir, TE_LOG_DEBUG);
        CHECK(hr == TE_S_OK);
        TE_LogShutdown();
    }

    SECTION("Write log entries and flush") {
        HRESULT hr = TE_LogInit(temp_dir, TE_LOG_DEBUG);
        REQUIRE(hr == TE_S_OK);

        TE_LogWrite(TE_LOG_INFO, "TestModule", "Hello from test");
        TE_LogWrite(TE_LOG_WARNING, "TestModule", "Warning message");
        TE_LogWrite(TE_LOG_ERROR, "TestModule", "Error message");

        TE_LogFlush();
        /* Give flush thread time to write */
        Sleep(200);
        TE_LogShutdown();

        /* Verify log file exists and has content */
        std::wstring log_file = std::wstring(temp_dir) + L"\\taskbarengine.log";
        FILE* f = _wfopen(log_file.c_str(), L"r");
        if (f) {
            char buf[1024];
            size_t bytes = fread(buf, 1, sizeof(buf) - 1, f);
            buf[bytes] = '\0';
            fclose(f);
            CHECK(bytes > 0);
            CHECK(strstr(buf, "Hello from test") != nullptr);
        }
    }

    SECTION("Null log dir returns error") {
        HRESULT hr = TE_LogInit(NULL, TE_LOG_DEBUG);
        CHECK(hr == TE_E_INVALIDARG);
    }

    SECTION("Multiple writes without crash") {
        HRESULT hr = TE_LogInit(temp_dir, TE_LOG_INFO);
        REQUIRE(hr == TE_S_OK);
        for (int i = 0; i < 500; i++) {
            TE_LogWrite(TE_LOG_INFO, "Stress", "Repeated log entry");
        }
        TE_LogFlush();
        Sleep(200);
        TE_LogShutdown();
    }

    /* Cleanup */
    std::wstring log_path = std::wstring(temp_dir) + L"\\taskbarengine.log";
    DeleteFileW(log_path.c_str());
    RemoveDirectoryW(temp_dir);
}
