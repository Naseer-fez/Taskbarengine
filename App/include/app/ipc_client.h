#pragma once
#include <sdk/te_types.h>
#include <sdk/te_ipc.h>

HRESULT TE_IpcClientSendCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len);
