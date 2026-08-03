#include "sdk/te_ipc.h"
#include <string.h>

HRESULT TE_IpcBuildHeader(TE_IpcHeader* out_header, TE_IpcMsgType type, uint32_t payload_length)
{
    if (!out_header) return E_POINTER;
    if (payload_length > TE_IPC_MAX_PAYLOAD) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    out_header->magic = TE_IPC_MAGIC;
    out_header->version = TE_IPC_VERSION;
    out_header->type = (uint32_t)type;
    out_header->payload_length = payload_length;
    return S_OK;
}

HRESULT TE_IpcValidateHeader(const TE_IpcHeader* header)
{
    if (!header) return E_POINTER;
    if (header->magic != TE_IPC_MAGIC) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (header->version != TE_IPC_VERSION) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    if (header->type == 0) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (header->payload_length > TE_IPC_MAX_PAYLOAD) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return S_OK;
}

HRESULT TE_IpcSerialize(void* buffer, size_t buffer_size, TE_IpcMsgType type, const void* payload, uint32_t payload_length, uint32_t* out_size)
{
    if (!buffer || !out_size) return E_POINTER;
    if (payload_length > 0 && !payload) return E_POINTER;
    if (payload_length > TE_IPC_MAX_PAYLOAD) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    size_t total = sizeof(TE_IpcHeader) + (size_t)payload_length;
    if (buffer_size < total) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    TE_IpcHeader header;
    HRESULT hr = TE_IpcBuildHeader(&header, type, payload_length);
    if (FAILED(hr)) return hr;

    memcpy(buffer, &header, sizeof(header));
    if (payload_length > 0) {
        memcpy((uint8_t*)buffer + sizeof(header), payload, payload_length);
    }

    *out_size = (uint32_t)total;
    return S_OK;
}

HRESULT TE_IpcDeserializeHeader(const void* buffer, size_t buffer_size, TE_IpcHeader* out_header)
{
    if (!buffer || !out_header) return E_POINTER;
    if (buffer_size < sizeof(TE_IpcHeader)) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);

    memcpy(out_header, buffer, sizeof(TE_IpcHeader));
    return TE_IpcValidateHeader(out_header);
}

HRESULT TE_IpcReadExact(HANDLE pipe, void* buffer, DWORD bytes)
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
