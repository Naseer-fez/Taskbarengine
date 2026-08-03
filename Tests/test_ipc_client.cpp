#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <app/ipc_client.h>
}

TEST_CASE("IPC client parameter validation and symbols", "[ipc_client]") {
    // 1. Validate that TE_IpcClientSendCommand returns E_POINTER if payload is NULL and payload_len > 0
    HRESULT hr = TE_IpcClientSendCommand(TE_IPC_MSG_ENABLE_PLUGIN, NULL, 10, NULL);
    REQUIRE(hr == E_POINTER);

    // 2. Simple sanity checks on the return of other functions when engine is not running
    // Since we're not running the server, they should fail (e.g. WaitNamedPipeW fails, yielding an HRESULT error)
    hr = TE_IpcClientShutdownEngine();
    REQUIRE(FAILED(hr));

    hr = TE_IpcClientReloadConfig();
    REQUIRE(FAILED(hr));
}
