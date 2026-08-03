#include "app/ipc_client.h"
#include <sdk/te_ipc.h>

HRESULT TE_IpcClientSendCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len, TE_IpcMsgType* out_response)
{
    if (payload_len > 0 && !payload) return E_POINTER;

    if (!WaitNamedPipeW(TE_PIPE_NAME, 1000)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HANDLE pipe = CreateFileW(TE_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    uint8_t buffer[sizeof(TE_IpcHeader) + TE_IPC_MAX_PAYLOAD];
    uint32_t total = 0;
    HRESULT hr = TE_IpcSerialize(buffer, sizeof(buffer), type, payload, payload_len, &total);
    if (SUCCEEDED(hr)) {
        DWORD written = 0;
        if (!WriteFile(pipe, buffer, total, &written, NULL) || written != total) {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }

    if (SUCCEEDED(hr) && out_response) {
        TE_IpcHeader response;
        hr = TE_IpcReadExact(pipe, &response, sizeof(response));
        if (SUCCEEDED(hr)) {
            hr = TE_IpcValidateHeader(&response);
        }
        if (SUCCEEDED(hr)) {
            if (response.payload_length > 0) {
                DWORD to_read = response.payload_length;
                while (to_read > 0) {
                    DWORD chunk = 0;
                    if (!ReadFile(pipe, buffer, min((DWORD)sizeof(buffer), to_read), &chunk, NULL)) {
                        hr = HRESULT_FROM_WIN32(GetLastError());
                        break;
                    }
                    if (chunk == 0) {
                        hr = HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
                        break;
                    }
                    to_read -= chunk;
                }
            }
            if (SUCCEEDED(hr)) {
                *out_response = (TE_IpcMsgType)response.type;
            }
        }
    }

    CloseHandle(pipe);
    return hr;
}

HRESULT TE_IpcClientShutdownEngine(void)
{
    TE_IpcMsgType response = (TE_IpcMsgType)0;
    HRESULT hr = TE_IpcClientSendCommand(TE_IPC_MSG_SHUTDOWN, NULL, 0, &response);
    if (FAILED(hr)) return hr;
    return (response == TE_IPC_MSG_SHUTDOWN_COMPLETE) ? S_OK : E_FAIL;
}

HRESULT TE_IpcClientReloadConfig(void)
{
    TE_IpcMsgType response = (TE_IpcMsgType)0;
    HRESULT hr = TE_IpcClientSendCommand(TE_IPC_MSG_RELOAD_CONFIG, NULL, 0, &response);
    if (FAILED(hr)) return hr;
    return (response == TE_IPC_MSG_STATUS) ? S_OK : E_FAIL;
}
