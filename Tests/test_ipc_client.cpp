#include <catch2/catch_test_macros.hpp>

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

    // 3. Simple sanity checks on the return of other functions when engine is not running
    // Since we're not running the server, they should fail gracefully (retry loop timeout)
    hr = TE_IpcClientShutdownEngine();
    REQUIRE(FAILED(hr));

    hr = TE_IpcClientReloadConfig();
    REQUIRE(FAILED(hr));
}
