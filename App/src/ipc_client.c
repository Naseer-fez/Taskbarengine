#include "app/ipc_client.h"
#include <sdk/te_ipc.h>

HRESULT TE_IpcClientSendCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len, TE_IpcMsgType* out_response)
{
    if (payload_len > 0 && !payload) return E_POINTER;
    if (payload_len > TE_IPC_MAX_PAYLOAD) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    const DWORD total_timeout_ms = 2000;
    DWORD start_tick = GetTickCount();

    for (int attempt = 0; attempt < 5; attempt++) {
        pipe = CreateFileW(
            TE_PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (pipe != INVALID_HANDLE_VALUE) {
            break;
        }

        DWORD err = GetLastError();
        if (err != ERROR_PIPE_BUSY && err != ERROR_FILE_NOT_FOUND) {
            return HRESULT_FROM_WIN32(err);
        }

        DWORD elapsed = GetTickCount() - start_tick;
        if (elapsed >= total_timeout_ms) {
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }

        DWORD remaining = total_timeout_ms - elapsed;
        DWORD wait_chunk = remaining > 200 ? 200 : remaining;
        if (!WaitNamedPipeW(TE_PIPE_NAME, wait_chunk)) {
            DWORD wait_err = GetLastError();
            if (wait_err != ERROR_SEM_TIMEOUT && wait_err != ERROR_FILE_NOT_FOUND) {
                return HRESULT_FROM_WIN32(wait_err);
            }
        }
    }

    if (pipe == INVALID_HANDLE_VALUE) {
        DWORD last_err = GetLastError();
        return HRESULT_FROM_WIN32(last_err == ERROR_SUCCESS ? ERROR_TIMEOUT : last_err);
    }

    size_t send_buf_size = sizeof(TE_IpcHeader) + (size_t)payload_len;
    uint8_t* send_buf = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, send_buf_size);
    if (!send_buf) {
        CloseHandle(pipe);
        return E_OUTOFMEMORY;
    }

    uint32_t total = 0;
    HRESULT hr = TE_IpcSerialize(send_buf, send_buf_size, type, payload, payload_len, &total);
    if (SUCCEEDED(hr)) {
        DWORD written = 0;
        if (!WriteFile(pipe, send_buf, total, &written, NULL) || written != total) {
            DWORD error = GetLastError();
            hr = HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT);
        }
    }
    HeapFree(GetProcessHeap(), 0, send_buf);

    if (SUCCEEDED(hr) && out_response) {
        TE_IpcHeader response;
        hr = TE_IpcReadExact(pipe, &response, sizeof(response));
        if (SUCCEEDED(hr)) {
            hr = TE_IpcValidateHeader(&response);
        }
        if (SUCCEEDED(hr)) {
            if (response.payload_length > 0) {
                if (response.payload_length > TE_IPC_MAX_PAYLOAD) {
                    hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                } else {
                    uint8_t* recv_buf = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, response.payload_length);
                    if (!recv_buf) {
                        hr = E_OUTOFMEMORY;
                    } else {
                        hr = TE_IpcReadExact(pipe, recv_buf, response.payload_length);
                        HeapFree(GetProcessHeap(), 0, recv_buf);
                    }
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
