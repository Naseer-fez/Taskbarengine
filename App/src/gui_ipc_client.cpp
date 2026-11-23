#include "gui_ipc_client.h"
#include <sdk/te_ipc.h>
#include <vector>
#include <algorithm>

namespace {
constexpr DWORD kConnectTimeoutMs = 500;
constexpr DWORD kIoTimeoutMs = 1500;

HRESULT PipeIo(HANDLE pipe, bool write, void* buffer, DWORD length, DWORD* transferred)
{
    if (transferred) *transferred = 0;
    if (length == 0) return S_OK;

    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent) return HRESULT_FROM_WIN32(GetLastError());

    DWORD immediate = 0;
    BOOL ok = write
        ? WriteFile(pipe, buffer, length, &immediate, &operation)
        : ReadFile(pipe, buffer, length, &immediate, &operation);
    if (!ok) {
        const DWORD ioError = GetLastError();
        if (ioError != ERROR_IO_PENDING) {
            CloseHandle(operation.hEvent);
            return HRESULT_FROM_WIN32(ioError);
        }

        const DWORD wait = WaitForSingleObject(operation.hEvent, kIoTimeoutMs);
        if (wait != WAIT_OBJECT_0) {
            const DWORD waitError = wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
            CancelIoEx(pipe, &operation);
            // Do not close the event or return while the kernel still owns the OVERLAPPED.
            WaitForSingleObject(operation.hEvent, INFINITE);
            DWORD ignored = 0;
            GetOverlappedResult(pipe, &operation, &ignored, FALSE);
            CloseHandle(operation.hEvent);
            return HRESULT_FROM_WIN32(waitError);
        }

        if (!GetOverlappedResult(pipe, &operation, &immediate, FALSE)) {
            const DWORD completionError = GetLastError();
            CloseHandle(operation.hEvent);
            return HRESULT_FROM_WIN32(completionError);
        }
    }

    if (transferred) *transferred = immediate;
    CloseHandle(operation.hEvent);
    return S_OK;
}

HRESULT PipeReadExact(HANDLE pipe, void* buffer, DWORD length)
{
    auto* bytes = static_cast<uint8_t*>(buffer);
    DWORD offset = 0;
    while (offset < length) {
        DWORD chunk = 0;
        HRESULT hr = PipeIo(pipe, false, bytes + offset, length - offset, &chunk);
        if (FAILED(hr)) return hr;
        if (chunk == 0) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        offset += chunk;
    }
    return S_OK;
}

HRESULT PipeWriteExact(HANDLE pipe, const void* buffer, DWORD length)
{
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    DWORD offset = 0;
    while (offset < length) {
        DWORD chunk = 0;
        HRESULT hr = PipeIo(pipe, true, const_cast<uint8_t*>(bytes + offset), length - offset, &chunk);
        if (FAILED(hr)) return hr;
        if (chunk == 0) return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        offset += chunk;
    }
    return S_OK;
}
}

static HRESULT SendIpcCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len, TE_IpcMsgType* out_response, std::string* out_data)
{
    if (payload_len > 0 && !payload) return E_POINTER;
    if (payload_len > TE_IPC_MAX_PAYLOAD) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    const DWORD total_timeout_ms = kConnectTimeoutMs;
    DWORD start_tick = GetTickCount();

    for (int attempt = 0; attempt < 8; attempt++) {
        pipe = CreateFileW(
            TE_PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
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
        DWORD wait_chunk = remaining > 100 ? 100 : remaining;
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
        hr = PipeWriteExact(pipe, buffer.data(), total);
    }

    if (SUCCEEDED(hr) && out_response) {
        TE_IpcHeader response;
        hr = PipeReadExact(pipe, &response, sizeof(response));
        if (SUCCEEDED(hr)) {
            hr = TE_IpcValidateHeader(&response);
        }
        if (SUCCEEDED(hr)) {
            if (response.payload_length > 0) {
                std::vector<char> data(response.payload_length + 1, 0);
                hr = PipeReadExact(pipe, data.data(), response.payload_length);
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
    if (WaitNamedPipeW(TE_PIPE_NAME, 0)) return true;
    DWORD err = GetLastError();
    if (err == ERROR_PIPE_BUSY) return true;
    return false;
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

HRESULT GuiIpcShutdown()
{
    TE_IpcMsgType response = (TE_IpcMsgType)0;
    HRESULT hr = SendIpcCommand(TE_IPC_MSG_SHUTDOWN, nullptr, 0, &response, nullptr);
    if (FAILED(hr)) return hr;
    return (response == TE_IPC_MSG_STATUS) ? S_OK : E_FAIL;
}

HRESULT GuiIpcEnablePlugin(const std::string& plugin_name)
{
    if (plugin_name.empty()) return E_INVALIDARG;
    TE_IpcMsgType response = (TE_IpcMsgType)0;
    HRESULT hr = SendIpcCommand(TE_IPC_MSG_ENABLE_PLUGIN, plugin_name.c_str(), static_cast<uint32_t>(plugin_name.length() + 1), &response, nullptr);
    if (FAILED(hr)) return hr;
    return (response == TE_IPC_MSG_STATUS) ? S_OK : E_FAIL;
}

HRESULT GuiIpcDisablePlugin(const std::string& plugin_name)
{
    if (plugin_name.empty()) return E_INVALIDARG;
    TE_IpcMsgType response = (TE_IpcMsgType)0;
    HRESULT hr = SendIpcCommand(TE_IPC_MSG_DISABLE_PLUGIN, plugin_name.c_str(), static_cast<uint32_t>(plugin_name.length() + 1), &response, nullptr);
    if (FAILED(hr)) return hr;
    return (response == TE_IPC_MSG_STATUS) ? S_OK : E_FAIL;
}
