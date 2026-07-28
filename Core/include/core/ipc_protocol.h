#pragma once

#include <sdk/te_ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_IpcBuildHeader(TE_IpcHeader* out_header, TE_IpcMsgType type, uint32_t payload_length);
HRESULT TE_IpcValidateHeader(const TE_IpcHeader* header);
HRESULT TE_IpcSerialize(void* buffer, size_t buffer_size, TE_IpcMsgType type, const void* payload, uint32_t payload_length, uint32_t* out_size);
HRESULT TE_IpcDeserializeHeader(const void* buffer, size_t buffer_size, TE_IpcHeader* out_header);

#ifdef __cplusplus
}
#endif
