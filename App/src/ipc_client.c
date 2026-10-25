#include "app/ipc_client.h"
#include <windows.h>
#include <stdio.h>

HRESULT TE_IpcClientSendCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len) {
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    const wchar_t* pipe_names[] = { TE_PIPE_NAME, TE_PIPE_NAME_LEGACY, L"\\\\.\\pipe\\TaskbarEngineIPC" };

    for (int retry = 0; retry < 5; retry++) {
        for (int p = 0; p < 3; p++) {
            hPipe = CreateFileW(pipe_names[p], GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (hPipe != INVALID_HANDLE_VALUE) break;
            if (GetLastError() == ERROR_PIPE_BUSY) {
                WaitNamedPipeW(pipe_names[p], 200);
                hPipe = CreateFileW(pipe_names[p], GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
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

    DWORD written = 0;
    if (!WriteFile(hPipe, &header, sizeof(header), &written, NULL)) {
        CloseHandle(hPipe);
        return TE_E_FAIL;
    }
    
    if (payload_len > 0 && payload != NULL) {
        if (!WriteFile(hPipe, payload, payload_len, &written, NULL)) {
            CloseHandle(hPipe);
            return TE_E_FAIL;
        }
    }

    FlushFileBuffers(hPipe);

    /* Read server response to complete synchronous handshake */
    TE_IpcHeader resp;
    DWORD read_bytes = 0;
    ReadFile(hPipe, &resp, sizeof(resp), &read_bytes, NULL);
    
    CloseHandle(hPipe);
    return TE_S_OK;
}
