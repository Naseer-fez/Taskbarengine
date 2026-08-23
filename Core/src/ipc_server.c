#include "core/ipc_server.h"
#include <sdk/te_ipc.h>
#include "core/core_manager.h"
#include "core/taskbar_subclass.h"
#include <sdk/te_log.h>
#include <sddl.h>

static HANDLE g_ipc_thread = NULL;
static HANDLE g_ipc_stop_event = NULL;
static HANDLE g_ipc_pipe = INVALID_HANDLE_VALUE;

static HRESULT IpcWriteMessage(HANDLE pipe, TE_IpcMsgType type, const void* payload, uint32_t payload_len)
{
    if (payload_len > TE_IPC_MAX_PAYLOAD) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    size_t buffer_size = sizeof(TE_IpcHeader) + (size_t)payload_len;
    uint8_t* buffer = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, buffer_size);
    if (!buffer) {
        return E_OUTOFMEMORY;
    }

    uint32_t total = 0;
    HRESULT hr = TE_IpcSerialize(buffer, buffer_size, type, payload, payload_len, &total);
    if (SUCCEEDED(hr)) {
        DWORD written = 0;
        if (!WriteFile(pipe, buffer, total, &written, NULL) || written != total) {
            DWORD error = GetLastError();
            hr = HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT);
        }
    }

    HeapFree(GetProcessHeap(), 0, buffer);
    return hr;
}

static bool IpcSendUiCommand(HWND taskbar_hwnd, WPARAM command, LPARAM parameter, DWORD_PTR* result)
{
    DWORD_PTR local_result = 0;
    if (!taskbar_hwnd || !IsWindow(taskbar_hwnd)) return false;

    LRESULT sent = SendMessageTimeoutW(taskbar_hwnd, WM_TE_IPC_COMMAND, command, parameter,
                                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &local_result);
    if (result) *result = local_result;
    return sent != 0;
}

static void IpcHandleMessage(HANDLE pipe, const TE_IpcHeader* header, const uint8_t* payload)
{
    HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
    DWORD_PTR command_result = 0;

    switch ((TE_IpcMsgType)header->type) {
        case TE_IPC_MSG_SHUTDOWN: {
            TE_LogWrite(TE_LOG_INFO, "IPC shutdown requested");
            if (taskbar_hwnd && IsWindow(taskbar_hwnd)) {
                if (!IpcSendUiCommand(taskbar_hwnd, TE_IPC_CMD_SHUTDOWN, 0, NULL)) {
                    IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "TIMEOUT", 7);
                    break;
                }
            } else {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "UNAVAILABLE", 11);
                break;
            }
            IpcWriteMessage(pipe, TE_IPC_MSG_SHUTDOWN_COMPLETE, NULL, 0);
            if (g_ipc_stop_event) SetEvent(g_ipc_stop_event);
            break;
        }

        case TE_IPC_MSG_RELOAD_CONFIG:
            if (taskbar_hwnd && IsWindow(taskbar_hwnd)) {
                if (!IpcSendUiCommand(taskbar_hwnd, TE_IPC_CMD_RELOAD_CONFIG, 0, NULL)) {
                    IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "TIMEOUT", 7);
                    break;
                }
            } else {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "UNAVAILABLE", 11);
                break;
            }
            IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "OK", 3);
            break;

        case TE_IPC_MSG_ENABLE_PLUGIN:
        case TE_IPC_MSG_DISABLE_PLUGIN: {
            if (!payload || header->payload_length == 0 || payload[0] == '\0') {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "INVALID", 8);
                break;
            }

            HRESULT hr;
            if (taskbar_hwnd && IsWindow(taskbar_hwnd)) {
                WPARAM cmd = (header->type == TE_IPC_MSG_ENABLE_PLUGIN) ? TE_IPC_CMD_ENABLE_PLUGIN : TE_IPC_CMD_DISABLE_PLUGIN;
                if (!IpcSendUiCommand(taskbar_hwnd, cmd, (LPARAM)payload, &command_result)) {
                    IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "TIMEOUT", 7);
                    break;
                }
                hr = (HRESULT)(LONG)command_result;
            } else {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "UNAVAILABLE", 11);
                break;
            }
            IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, SUCCEEDED(hr) ? "OK" : "ERROR", SUCCEEDED(hr) ? 3 : 6);
            break;
        }

        case TE_IPC_MSG_GET_PLUGIN_LIST: {
            char* list = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 2048);
            if (!list) {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "ERROR", 6);
                break;
            }

            TE_IpcSyncPayload sync_payload = { 0 };
            sync_payload.buffer = list;
            sync_payload.buffer_len = 2048;
            if (taskbar_hwnd && IsWindow(taskbar_hwnd)) {
                if (!IpcSendUiCommand(taskbar_hwnd, TE_IPC_CMD_GET_PLUGIN_LIST, (LPARAM)&sync_payload, NULL)) {
                    IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "TIMEOUT", 7);
                    HeapFree(GetProcessHeap(), 0, list);
                    break;
                }
            } else {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "UNAVAILABLE", 11);
                HeapFree(GetProcessHeap(), 0, list);
                break;
            }
            IpcWriteMessage(pipe, TE_IPC_MSG_PLUGIN_LIST, list, sync_payload.result_code);
            HeapFree(GetProcessHeap(), 0, list);
            break;
        }

        case TE_IPC_MSG_GET_SETTINGS: {
            char* schema = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, TE_IPC_MAX_PAYLOAD);
            if (!schema) {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "ERROR", 6);
                break;
            }
            TE_IpcSyncPayload sync_payload = { 0 };
            sync_payload.buffer = schema;
            sync_payload.buffer_len = TE_IPC_MAX_PAYLOAD;
            if (taskbar_hwnd && IsWindow(taskbar_hwnd)) {
                if (!IpcSendUiCommand(taskbar_hwnd, TE_IPC_CMD_GET_SETTINGS, (LPARAM)&sync_payload, NULL)) {
                    IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "TIMEOUT", 7);
                    HeapFree(GetProcessHeap(), 0, schema);
                    break;
                }
            } else {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "UNAVAILABLE", 11);
                HeapFree(GetProcessHeap(), 0, schema);
                break;
            }
            IpcWriteMessage(pipe, TE_IPC_MSG_SETTINGS_RESPONSE, schema, sync_payload.result_code);
            HeapFree(GetProcessHeap(), 0, schema);
            break;
        }

        case TE_IPC_MSG_GET_PERF_STATS: {
            char* stats = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 4096);
            if (!stats) {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "ERROR", 6);
                break;
            }
            TE_IpcSyncPayload sync_payload = { 0 };
            sync_payload.buffer = stats;
            sync_payload.buffer_len = 4096;
            if (taskbar_hwnd && IsWindow(taskbar_hwnd)) {
                if (!IpcSendUiCommand(taskbar_hwnd, TE_IPC_CMD_GET_PERF_STATS, (LPARAM)&sync_payload, NULL)) {
                    IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "TIMEOUT", 7);
                    HeapFree(GetProcessHeap(), 0, stats);
                    break;
                }
            } else {
                IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "UNAVAILABLE", 11);
                HeapFree(GetProcessHeap(), 0, stats);
                break;
            }
            IpcWriteMessage(pipe, TE_IPC_MSG_PERF_STATS_RESPONSE, stats, sync_payload.result_code);
            HeapFree(GetProcessHeap(), 0, stats);
            break;
        }

        default:
            IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "UNSUPPORTED", 12);
            break;
    }
}

static DWORD WINAPI IpcServerThreadProc(LPVOID param)
{
    (void)param;

    for (;;) {
        if (WaitForSingleObject(g_ipc_stop_event, 0) == WAIT_OBJECT_0) break;

        SECURITY_ATTRIBUTES sa;
        PSECURITY_DESCRIPTOR sd = NULL;
        ZeroMemory(&sa, sizeof(sa));
        sa.nLength = sizeof(sa);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;GA;;;OW)(A;;GA;;;SY)",
                SDDL_REVISION_1,
                &sd,
                NULL)) {
            TE_LogWrite(TE_LOG_ERROR, "Failed to create IPC security descriptor (%lu)", GetLastError());
            break;
        }
        sa.lpSecurityDescriptor = sd;

        g_ipc_pipe = CreateNamedPipeW(
            TE_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1,
            sizeof(TE_IpcHeader) + TE_IPC_MAX_PAYLOAD,
            sizeof(TE_IpcHeader) + TE_IPC_MAX_PAYLOAD,
            0,
            sa.lpSecurityDescriptor ? &sa : NULL);

        if (sd) {
            LocalFree(sd);
        }

        if (g_ipc_pipe == INVALID_HANDLE_VALUE) {
            TE_LogWrite(TE_LOG_ERROR, "CreateNamedPipe failed (%lu)", GetLastError());
            break;
        }

        BOOL connected = ConnectNamedPipe(g_ipc_pipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected) {
            for (;;) {
                if (WaitForSingleObject(g_ipc_stop_event, 0) == WAIT_OBJECT_0) break;
                TE_IpcHeader header;
                HRESULT hr = TE_IpcReadExact(g_ipc_pipe, &header, sizeof(header));
                if (FAILED(hr) || FAILED(TE_IpcValidateHeader(&header))) break;

                uint8_t* payload = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, (size_t)header.payload_length + 1);
                if (!payload) break;
                if (header.payload_length > 0) {
                    hr = TE_IpcReadExact(g_ipc_pipe, payload, header.payload_length);
                    if (FAILED(hr)) {
                        HeapFree(GetProcessHeap(), 0, payload);
                        break;
                    }
                }
                payload[header.payload_length] = '\0';
                IpcHandleMessage(g_ipc_pipe, &header, payload);
                HeapFree(GetProcessHeap(), 0, payload);
            }
        }

        DisconnectNamedPipe(g_ipc_pipe);
        CloseHandle(g_ipc_pipe);
        g_ipc_pipe = INVALID_HANDLE_VALUE;
    }

    return 0;
}

HRESULT TE_IpcServerStart(void)
{
    if (g_ipc_thread) return S_OK;

    g_ipc_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_ipc_stop_event) return HRESULT_FROM_WIN32(GetLastError());

    g_ipc_thread = CreateThread(NULL, 0, IpcServerThreadProc, NULL, 0, NULL);
    if (!g_ipc_thread) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(g_ipc_stop_event);
        g_ipc_stop_event = NULL;
        return hr;
    }

    return S_OK;
}

void TE_IpcServerStop(void)
{
    if (g_ipc_stop_event) {
        SetEvent(g_ipc_stop_event);
    }

    if (g_ipc_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_ipc_pipe, NULL);
    }

    if (g_ipc_thread) {
        if (GetThreadId(g_ipc_thread) == GetCurrentThreadId()) {
            CloseHandle(g_ipc_thread);
            g_ipc_thread = NULL;
            if (g_ipc_stop_event) {
                CloseHandle(g_ipc_stop_event);
                g_ipc_stop_event = NULL;
            }
            return;
        }
        CancelSynchronousIo(g_ipc_thread);
        WaitForSingleObject(g_ipc_thread, INFINITE);
        CloseHandle(g_ipc_thread);
        g_ipc_thread = NULL;
    }

    if (g_ipc_stop_event) {
        CloseHandle(g_ipc_stop_event);
        g_ipc_stop_event = NULL;
    }
}
