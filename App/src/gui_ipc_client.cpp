#include "gui_ipc_client.h"
#include <sdk/te_ipc.h>
#include <vector>

static HRESULT SendIpcCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len, TE_IpcMsgType* out_response, std::string* out_data)
{
    if (payload_len > 0 && !payload) return E_POINTER;
    if (payload_len > TE_IPC_MAX_PAYLOAD) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    const DWORD total_timeout_ms = 50;
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

    size_t send_buf_size = sizeof(TE_IpcHeader) + static_cast<size_t>(payload_len);
    std::vector<uint8_t> buffer(send_buf_size);
    uint32_t total = 0;
    HRESULT hr = TE_IpcSerialize(buffer.data(), buffer.size(), type, payload, payload_len, &total);
    if (SUCCEEDED(hr)) {
        DWORD written = 0;
        if (!WriteFile(pipe, buffer.data(), total, &written, NULL) || written != total) {
            DWORD error = GetLastError();
            hr = HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT);
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
                std::vector<char> data(response.payload_length + 1, 0);
                DWORD to_read = response.payload_length;
                DWORD offset = 0;
                while (to_read > 0) {
                    DWORD chunk = 0;
                    if (!ReadFile(pipe, data.data() + offset, to_read, &chunk, NULL)) {
                        hr = HRESULT_FROM_WIN32(GetLastError());
                        break;
                    }
                    if (chunk == 0) {
                        hr = HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
                        break;
                    }
                    to_read -= chunk;
                    offset += chunk;
                }
                if (SUCCEEDED(hr) && out_data) {
                    *out_data = std::string(data.data(), response.payload_length);
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

bool GuiIpcIsConnected()
{
    return WaitNamedPipeW(TE_PIPE_NAME, 100) != 0;
}

HRESULT GuiIpcReloadConfig()
{
    TE_IpcMsgType response = (TE_IpcMsgType)0;
    HRESULT hr = SendIpcCommand(TE_IPC_MSG_RELOAD_CONFIG, nullptr, 0, &response, nullptr);
    if (FAILED(hr)) return hr;
    return (response == TE_IPC_MSG_STATUS) ? S_OK : E_FAIL;
}

std::optional<std::string> GuiIpcGetSettings()
{
    TE_IpcMsgType response = (TE_IpcMsgType)0;
    std::string data;
    HRESULT hr = SendIpcCommand(TE_IPC_MSG_GET_SETTINGS, nullptr, 0, &response, &data);
    if (SUCCEEDED(hr) && response == TE_IPC_MSG_SETTINGS_RESPONSE) {
        return data;
    }
    return std::nullopt;
}

std::optional<std::string> GuiIpcGetPerfStats()
{
    TE_IpcMsgType response = (TE_IpcMsgType)0;
    std::string data;
    HRESULT hr = SendIpcCommand(TE_IPC_MSG_GET_PERF_STATS, nullptr, 0, &response, &data);
    if (SUCCEEDED(hr) && response == TE_IPC_MSG_PERF_STATS_RESPONSE) {
        return data;
    }
    return std::nullopt;
}

std::optional<std::string> GuiIpcGetPluginList()
{
    TE_IpcMsgType response = (TE_IpcMsgType)0;
    std::string data;
    HRESULT hr = SendIpcCommand(TE_IPC_MSG_GET_PLUGIN_LIST, nullptr, 0, &response, &data);
    if (SUCCEEDED(hr) && response == TE_IPC_MSG_PLUGIN_LIST) {
        return data;
    }
    return std::nullopt;
}
