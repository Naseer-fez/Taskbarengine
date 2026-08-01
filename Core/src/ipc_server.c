#include "core/ipc_server.h"
#include "core/ipc_protocol.h"
#include "core/core_manager.h"
#include "core/taskbar_subclass.h"
#include <sdk/te_log.h>
#include <sddl.h>

#define TE_PIPE_NAME L"\\\\.\\pipe\\TaskbarEngine"

static HANDLE g_ipc_thread = NULL;
static HANDLE g_ipc_stop_event = NULL;
static HANDLE g_ipc_pipe = INVALID_HANDLE_VALUE;

static HRESULT IpcWriteMessage(HANDLE pipe, TE_IpcMsgType type, const void* payload, uint32_t payload_len)
{
    uint8_t buffer[sizeof(TE_IpcHeader) + TE_IPC_MAX_PAYLOAD];
    uint32_t total = 0;
    HRESULT hr = TE_IpcSerialize(buffer, sizeof(buffer), type, payload, payload_len, &total);
    if (FAILED(hr)) return hr;

    DWORD written = 0;
    if (!WriteFile(pipe, buffer, total, &written, NULL) || written != total) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

static HRESULT IpcReadExact(HANDLE pipe, void* buffer, DWORD bytes)
{
    DWORD total = 0;
    while (total < bytes) {
        DWORD read_now = 0;
        if (!ReadFile(pipe, (uint8_t*)buffer + total, bytes - total, &read_now, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE) return HRESULT_FROM_WIN32(err);
            return HRESULT_FROM_WIN32(err);
        }
        if (read_now == 0) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        total += read_now;
    }
    return S_OK;
}

static void IpcHandleMessage(HANDLE pipe, const TE_IpcHeader* header, const uint8_t* payload)
{
    HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);

    switch ((TE_IpcMsgType)header->type) {
        case TE_IPC_MSG_SHUTDOWN:
            TE_LogWrite(TE_LOG_INFO, "IPC shutdown requested");
            TE_CoreManagerShutdownFromIpc();
            IpcWriteMessage(pipe, TE_IPC_MSG_SHUTDOWN_COMPLETE, NULL, 0);
            if (g_ipc_stop_event) SetEvent(g_ipc_stop_event);
            break;

        case TE_IPC_MSG_RELOAD_CONFIG:
            if (taskbar_hwnd && IsWindow(taskbar_hwnd)) {
                PostMessageW(taskbar_hwnd, WM_TE_IPC_COMMAND, TE_IPC_CMD_RELOAD_CONFIG, 0);
            }
            IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "OK", 3);
            break;

        case TE_IPC_MSG_ENABLE_PLUGIN:
        case TE_IPC_MSG_DISABLE_PLUGIN: {
            char* name_dup = NULL;
            if (payload && payload[0] != '\0') {
                size_t len = strlen((const char*)payload) + 1;
                name_dup = (char*)HeapAlloc(GetProcessHeap(), 0, len);
                if (name_dup) {
                    memcpy(name_dup, payload, len);
                }
            }
            if (taskbar_hwnd && IsWindow(taskbar_hwnd) && name_dup) {
                WPARAM cmd = (header->type == TE_IPC_MSG_ENABLE_PLUGIN) ? TE_IPC_CMD_ENABLE_PLUGIN : TE_IPC_CMD_DISABLE_PLUGIN;
                PostMessageW(taskbar_hwnd, WM_TE_IPC_COMMAND, cmd, (LPARAM)name_dup);
            } else if (name_dup) {
                HeapFree(GetProcessHeap(), 0, name_dup);
            }
            IpcWriteMessage(pipe, TE_IPC_MSG_STATUS, "OK", 3);
            break;
        }

        case TE_IPC_MSG_GET_PLUGIN_LIST: {
            char list[2048];
            uint32_t len = TE_CoreManagerBuildPluginList(list, sizeof(list));
            IpcWriteMessage(pipe, TE_IPC_MSG_PLUGIN_LIST, list, len);
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
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;GA;;;OW)(A;;GA;;;SY)",
                SDDL_REVISION_1,
                &sd,
                NULL)) {
            sa.lpSecurityDescriptor = sd;
        }

        g_ipc_pipe = CreateNamedPipeW(
            TE_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
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
                HRESULT hr = IpcReadExact(g_ipc_pipe, &header, sizeof(header));
                if (FAILED(hr) || FAILED(TE_IpcValidateHeader(&header))) break;

                uint8_t payload[TE_IPC_MAX_PAYLOAD + 1];
                if (header.payload_length > 0) {
                    hr = IpcReadExact(g_ipc_pipe, payload, header.payload_length);
                    if (FAILED(hr)) break;
                }
                payload[header.payload_length] = '\0';
                IpcHandleMessage(g_ipc_pipe, &header, payload);
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
            return;
        }
        WaitForSingleObject(g_ipc_thread, 1000);
        CloseHandle(g_ipc_thread);
        g_ipc_thread = NULL;
    }

    if (g_ipc_stop_event) {
        CloseHandle(g_ipc_stop_event);
        g_ipc_stop_event = NULL;
    }
}
