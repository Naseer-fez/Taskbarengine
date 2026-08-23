#include <catch2/catch_test_macros.hpp>
#include <windows.h>

extern "C" {
#include <app/ipc_client.h>
}

TEST_CASE("IPC client parameter validation and symbols", "[ipc_client]") {
    // 1. Validate that TE_IpcClientSendCommand returns E_POINTER if payload is NULL and payload_len > 0
    HRESULT hr = TE_IpcClientSendCommand(TE_IPC_MSG_ENABLE_PLUGIN, NULL, 10, NULL);
    REQUIRE(hr == E_POINTER);

    // 2. Validate that TE_IpcClientSendCommand rejects payload exceeding TE_IPC_MAX_PAYLOAD
    const char dummy[] = "payload";
    hr = TE_IpcClientSendCommand(TE_IPC_MSG_ENABLE_PLUGIN, dummy, 65536 + 1, NULL);
    REQUIRE(FAILED(hr));

    // 3. Validate TE_IpcClientGetSettingsSchema pointer validation
    hr = TE_IpcClientGetSettingsSchema(NULL);
    REQUIRE(hr == E_POINTER);
}

TEST_CASE("IPC queries when engine is running", "[ipc_client][integration]") {
    char env_val[32] = {0};
    GetEnvironmentVariableA("TE_INTEGRATION_TESTS", env_val, sizeof(env_val));
    if (strcmp(env_val, "1") != 0) {
        SKIP("Integration tests disabled. Set TE_INTEGRATION_TESTS=1 to run.");
    }

    char* settings = NULL;
    HRESULT hr = TE_IpcClientGetSettingsSchema(&settings);
    REQUIRE(SUCCEEDED(hr));
    if (settings) {
        TE_IpcClientFreeBuffer(settings);
    }
}
