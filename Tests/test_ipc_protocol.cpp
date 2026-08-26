#include <catch2/catch_test_macros.hpp>
#include <sdk/te_ipc.h>
#include <string.h>

TEST_CASE("IPC Protocol Serialization", "[ipc]") {
    SECTION("Build and validate header") {
        TE_IpcHeader header;
        REQUIRE(TE_IpcBuildHeader(&header, TE_IPC_MSG_STATUS, 100) == TE_S_OK);
        REQUIRE(header.magic == TE_IPC_MAGIC);
        REQUIRE(header.version == TE_IPC_VERSION);
        REQUIRE(header.type == TE_IPC_MSG_STATUS);
        REQUIRE(header.payload_length == 100);
        
        REQUIRE(TE_IpcValidateHeader(&header) == TE_S_OK);
    }
    
    SECTION("Invalid magic") {
        TE_IpcHeader header;
        TE_IpcBuildHeader(&header, TE_IPC_MSG_STATUS, 100);
        header.magic = 0x12345678;
        REQUIRE(TE_IpcValidateHeader(&header) == TE_E_FAIL);
    }
    
    SECTION("Payload too large") {
        TE_IpcHeader header;
        REQUIRE(TE_IpcBuildHeader(&header, TE_IPC_MSG_STATUS, TE_IPC_MAX_PAYLOAD + 1) == TE_E_FAIL);
    }
}
