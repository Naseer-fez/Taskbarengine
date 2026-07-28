#include <catch2/catch_test_macros.hpp>
#include <sdk/te_log_impl.h>
#include <sdk/te_log.h>
#include <windows.h>

#include <thread>
#include <vector>

TEST_CASE("Ring Buffer Logger tests", "[log]") {
    SECTION("Struct size check") {
        REQUIRE(sizeof(TE_LogEntry) == 256);
    }

    SECTION("Log Init, Multi-write, and Shutdown") {
        wchar_t temp_path[MAX_PATH];
        GetTempPathW(MAX_PATH, temp_path);
        wcscat(temp_path, L"TE_LogTestDir");

        HRESULT hr = TE_LogInit(temp_path, TE_LOG_DEBUG, false); /* File write false for unit test memory check */
        REQUIRE(SUCCEEDED(hr));

        for (int i = 0; i < 300; i++) {
            TE_LogWrite(TE_LOG_INFO, "Sequential log entry %d", i);
        }

        TE_LogShutdown();
    }

    SECTION("Multi-threaded Stress Test") {
        wchar_t temp_path[MAX_PATH];
        GetTempPathW(MAX_PATH, temp_path);
        wcscat(temp_path, L"TE_LogStressDir");

        HRESULT hr = TE_LogInit(temp_path, TE_LOG_DEBUG, false);
        REQUIRE(SUCCEEDED(hr));

        std::vector<std::thread> threads;
        for (int t = 0; t < 4; t++) {
            threads.emplace_back([t]() {
                for (int i = 0; i < 200; i++) {
                    TE_LogWrite(TE_LOG_INFO, "Thread %d log %d", t, i);
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        TE_LogShutdown();
    }
}
