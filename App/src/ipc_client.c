#include "app/ipc_client.h"
#include <windows.h>
#include <stdio.h>

HRESULT TE_IpcClientSendCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len) {
    HANDLE hPipe = CreateFileW(TE_PIPE_NAME, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) {
        return TE_E_FAIL;
    }
    
    TE_IpcHeader header = { TE_IPC_MAGIC, 1, type, payload_len };
    DWORD written;
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
    
    CloseHandle(hPipe);
    return TE_S_OK;
}
