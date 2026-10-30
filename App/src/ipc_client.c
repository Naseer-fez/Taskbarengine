#include "app/ipc_client.h"
#include <windows.h>
#include <stdio.h>

HRESULT TE_IpcClientSendCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len) {
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    const wchar_t* pipe_names[] = { TE_PIPE_NAME, TE_PIPE_NAME_LEGACY, L"\\\\.\\pipe\\TaskbarEngineIPC" };

    for (int retry = 0; retry < 5; retry++) {
        for (int p = 0; p < 3; p++) {
            hPipe = CreateFileW(pipe_names[p], GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (hPipe != INVALID_HANDLE_VALUE) break;
            if (GetLastError() == ERROR_PIPE_BUSY) {
                WaitNamedPipeW(pipe_names[p], 200);
                hPipe = CreateFileW(pipe_names[p], GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
                if (hPipe != INVALID_HANDLE_VALUE) break;
            }
        }
        if (hPipe != INVALID_HANDLE_VALUE) break;
        Sleep(50);
    }
    if (hPipe == INVALID_HANDLE_VALUE) {
        return TE_E_FAIL;
    }
    
    TE_IpcHeader header;
    if (FAILED(TE_IpcBuildHeader(&header, type, payload_len))) {
        CloseHandle(hPipe);
        return TE_E_FAIL;
    }

    OVERLAPPED ol = {0};
    ol.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ol.hEvent) {
        CloseHandle(hPipe);
        return TE_E_FAIL;
    }

    DWORD written = 0;
    if (!WriteFile(hPipe, &header, sizeof(header), &written, &ol)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            DWORD wait_res = WaitForSingleObject(ol.hEvent, 3000);
            if (wait_res != WAIT_OBJECT_0 || !GetOverlappedResult(hPipe, &ol, &written, FALSE)) {
                CancelIoEx(hPipe, &ol);
                DWORD dummy = 0;
                GetOverlappedResult(hPipe, &ol, &dummy, TRUE);
                CloseHandle(ol.hEvent);
                CloseHandle(hPipe);
                return TE_E_FAIL;
            }
        } else {
            CloseHandle(ol.hEvent);
            CloseHandle(hPipe);
            return TE_E_FAIL;
        }
    }
    
    if (payload_len > 0 && payload != NULL) {
        ResetEvent(ol.hEvent);
        ol.Offset = 0; ol.OffsetHigh = 0;
        if (!WriteFile(hPipe, payload, payload_len, &written, &ol)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                DWORD wait_res = WaitForSingleObject(ol.hEvent, 3000);
                if (wait_res != WAIT_OBJECT_0 || !GetOverlappedResult(hPipe, &ol, &written, FALSE)) {
                    CancelIoEx(hPipe, &ol);
                    DWORD dummy = 0;
                    GetOverlappedResult(hPipe, &ol, &dummy, TRUE);
                    CloseHandle(ol.hEvent);
                    CloseHandle(hPipe);
                    return TE_E_FAIL;
                }
            } else {
                CloseHandle(ol.hEvent);
                CloseHandle(hPipe);
                return TE_E_FAIL;
            }
        }
    }

    FlushFileBuffers(hPipe);

    /* Read server response to complete synchronous handshake */
    ResetEvent(ol.hEvent);
    ol.Offset = 0; ol.OffsetHigh = 0;
    TE_IpcHeader resp;
    DWORD read_bytes = 0;
    if (!ReadFile(hPipe, &resp, sizeof(resp), &read_bytes, &ol)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            DWORD wait_res = WaitForSingleObject(ol.hEvent, 3000);
            if (wait_res != WAIT_OBJECT_0 || !GetOverlappedResult(hPipe, &ol, &read_bytes, FALSE)) {
                CancelIoEx(hPipe, &ol);
                DWORD dummy = 0;
                GetOverlappedResult(hPipe, &ol, &dummy, TRUE);
            }
        }
    }
    
    CloseHandle(ol.hEvent);
    CloseHandle(hPipe);
    return TE_S_OK;
}
