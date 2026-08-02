#include "gui_ipc_client.h"
#include <core/ipc_protocol.h>
#include <vector>

#define TE_PIPE_NAME L"\\\\.\\pipe\\TaskbarEngine"

static HRESULT ReadExact(HANDLE pipe, void* buffer, DWORD bytes)
{
    DWORD total = 0;
    while (total < bytes) {
        DWORD read_now = 0;
        if (!ReadFile(pipe, (uint8_t*)buffer + total, bytes - total, &read_now, NULL)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (read_now == 0) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        total += read_now;
    }
    return S_OK;
}

static HRESULT SendIpcCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len, TE_IpcMsgType* out_response, std::string* out_data)
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
        hr = ReadExact(pipe, &response, sizeof(response));
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
