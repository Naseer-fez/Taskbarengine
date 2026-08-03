#include <catch2/catch_test_macros.hpp>
#include <sdk/te_ipc.h>
#include <core/taskbar_subclass.h>

TEST_CASE("IPC protocol serializes and validates messages", "[ipc]") {
    uint8_t buffer[sizeof(TE_IpcHeader) + 32]{};
    const char payload[] = "taskbar_resize";
    uint32_t size = 0;

    HRESULT hr = TE_IpcSerialize(buffer, sizeof(buffer), TE_IPC_MSG_ENABLE_PLUGIN, payload, sizeof(payload), &size);
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(size == sizeof(TE_IpcHeader) + sizeof(payload));

    TE_IpcHeader header{};
    hr = TE_IpcDeserializeHeader(buffer, size, &header);
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(header.magic == TE_IPC_MAGIC);
    REQUIRE(header.version == TE_IPC_VERSION);
    REQUIRE(header.type == TE_IPC_MSG_ENABLE_PLUGIN);
    REQUIRE(header.payload_length == sizeof(payload));
}

TEST_CASE("IPC protocol rejects malformed headers", "[ipc]") {
    TE_IpcHeader header{};
    REQUIRE(SUCCEEDED(TE_IpcBuildHeader(&header, TE_IPC_MSG_SHUTDOWN, 0)));

    header.magic = 0;
    REQUIRE(FAILED(TE_IpcValidateHeader(&header)));

    REQUIRE(FAILED(TE_IpcDeserializeHeader(&header, sizeof(header) - 1, &header)));

    REQUIRE(FAILED(TE_IpcBuildHeader(&header, TE_IPC_MSG_STATUS, TE_IPC_MAX_PAYLOAD + 1)));
}

TEST_CASE("IPC subclass command constants and sync payload struct", "[ipc]") {
    REQUIRE(TE_IPC_CMD_RELOAD_CONFIG == 1);
    REQUIRE(TE_IPC_CMD_ENABLE_PLUGIN == 2);
    REQUIRE(TE_IPC_CMD_DISABLE_PLUGIN == 3);
    REQUIRE(TE_IPC_CMD_SHUTDOWN == 4);
    REQUIRE(TE_IPC_CMD_GET_PLUGIN_LIST == 5);

    TE_IpcSyncPayload sync{};
    sync.completion_event = (HANDLE)(uintptr_t)0x1234;
    char buffer[64] = "test";
    sync.buffer = buffer;
    sync.buffer_len = sizeof(buffer);
    sync.result_code = 42;

    REQUIRE(sync.completion_event == (HANDLE)(uintptr_t)0x1234);
    REQUIRE(sync.buffer == buffer);
    REQUIRE(sync.buffer_len == sizeof(buffer));
    REQUIRE(sync.result_code == 42);
}
