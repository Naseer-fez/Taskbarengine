#pragma once

#include <sdk/te_ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_IpcClientSendCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len, TE_IpcMsgType* out_response);
HRESULT TE_IpcClientShutdownEngine(void);
HRESULT TE_IpcClientReloadConfig(void);
HRESULT TE_IpcClientGetSettingsSchema(char** out_schema);
void TE_IpcClientFreeBuffer(void* buffer);

#ifdef __cplusplus
}
#endif
