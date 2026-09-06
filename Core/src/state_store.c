#include <windows.h>
#include <string.h>
#include <stdint.h>
#include <core/state_store.h>
#include <sdk/te_log.h>

#define TE_STATE_STORE_CAPACITY 256

typedef struct {
    char key[64];
    StateValue value;
    int occupied;
} StateEntry;

static StateEntry g_StateTable[TE_STATE_STORE_CAPACITY];
static SRWLOCK g_StateLock = SRWLOCK_INIT;

// FNV-1a hash function
static uint32_t HashKey(const char* key) {
    uint32_t hash = 2166136261u;
    while (*key) {
        hash ^= (uint8_t)(*key++);
        hash *= 16777619u;
    }
    return hash;
}

HRESULT TE_StateStoreInit(void) {
    InitializeSRWLock(&g_StateLock);
    memset(g_StateTable, 0, sizeof(g_StateTable));
    return TE_S_OK;
}

void TE_StateStoreShutdown(void) {
    AcquireSRWLockExclusive(&g_StateLock);
    memset(g_StateTable, 0, sizeof(g_StateTable));
    ReleaseSRWLockExclusive(&g_StateLock);
}

HRESULT TE_StatePublish(const char* key, const StateValue* value) {
    if (!key || !value) {
        return TE_E_INVALIDARG;
    }

    uint32_t hash = HashKey(key);
    uint32_t index = hash % TE_STATE_STORE_CAPACITY;
    uint32_t startIndex = index;
    int found = 0;

    AcquireSRWLockExclusive(&g_StateLock);

    // Linear probing
    do {
        if (!g_StateTable[index].occupied || strncmp(g_StateTable[index].key, key, 63) == 0) {
            strncpy_s(g_StateTable[index].key, sizeof(g_StateTable[index].key), key, _TRUNCATE);
            g_StateTable[index].value = *value;
            g_StateTable[index].occupied = 1;
            found = 1;
            break;
        }
        index = (index + 1) % TE_STATE_STORE_CAPACITY;
    } while (index != startIndex);

    ReleaseSRWLockExclusive(&g_StateLock);

    if (found) {
        return TE_S_OK;
    }
    
    return TE_E_OUTOFMEMORY;
}

HRESULT TE_StateQuery(const char* key, StateValue* out_value) {
    if (!key || !out_value) {
        return TE_E_INVALIDARG;
    }

    uint32_t hash = HashKey(key);
    uint32_t index = hash % TE_STATE_STORE_CAPACITY;
    uint32_t startIndex = index;
    HRESULT hr = TE_E_FAIL;

    AcquireSRWLockShared(&g_StateLock);

    // Linear probing
    do {
        if (g_StateTable[index].occupied) {
            if (strncmp(g_StateTable[index].key, key, 63) == 0) {
                *out_value = g_StateTable[index].value;
                hr = TE_S_OK;
                break;
            }
        } else {
            // Hit an empty slot, means the key is not in the map
            break;
        }
        index = (index + 1) % TE_STATE_STORE_CAPACITY;
    } while (index != startIndex);

    ReleaseSRWLockShared(&g_StateLock);

    return hr;
}
