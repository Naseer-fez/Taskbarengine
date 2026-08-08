#include "core/state_store.h"
#include <string.h>
#include <windows.h>

typedef struct StateEntry {
    char key[TE_STATE_KEY_MAX_LEN];
    StateValue val;
    bool occupied;
} StateEntry;

static StateEntry g_entries[TE_STATE_STORE_MAX_ENTRIES];
static SRWLOCK g_lock = SRWLOCK_INIT;
static bool g_initialized = false;

static uint32_t HashFnv1a(const char* str)
{
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)(*str++);
        hash *= 16777619u;
    }
    return hash;
}

HRESULT TE_StateStoreInit(void)
{
    AcquireSRWLockExclusive(&g_lock);
    memset(g_entries, 0, sizeof(g_entries));
    g_initialized = true;
    ReleaseSRWLockExclusive(&g_lock);
    return S_OK;
}

void TE_StateStoreShutdown(void)
{
    AcquireSRWLockExclusive(&g_lock);
    memset(g_entries, 0, sizeof(g_entries));
    g_initialized = false;
    ReleaseSRWLockExclusive(&g_lock);
}

HRESULT TE_StatePublish(const char* key, const StateValue* val)
{
    if (!key || !val) return E_POINTER;
    if (key[0] == '\0') return E_INVALIDARG;
    if (strnlen_s(key, TE_STATE_KEY_MAX_LEN) == TE_STATE_KEY_MAX_LEN) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);

    AcquireSRWLockExclusive(&g_lock);
    if (!g_initialized) {
        ReleaseSRWLockExclusive(&g_lock);
        return E_UNEXPECTED;
    }

    uint32_t hash = HashFnv1a(key);
    uint32_t start = hash % TE_STATE_STORE_MAX_ENTRIES;
    uint32_t idx = start;
    int first_empty = -1;

    do {
        if (g_entries[idx].occupied) {
            if (strcmp(g_entries[idx].key, key) == 0) {
                /* Update existing */
                g_entries[idx].val = *val;
                ReleaseSRWLockExclusive(&g_lock);
                return S_OK;
            }
        } else if (first_empty == -1) {
            first_empty = (int)idx;
        }
        idx = (idx + 1) % TE_STATE_STORE_MAX_ENTRIES;
    } while (idx != start);

    if (first_empty != -1) {
        strncpy_s(g_entries[first_empty].key, TE_STATE_KEY_MAX_LEN, key, _TRUNCATE);
        g_entries[first_empty].val = *val;
        g_entries[first_empty].occupied = true;
        ReleaseSRWLockExclusive(&g_lock);
        return S_OK;
    }

    ReleaseSRWLockExclusive(&g_lock);
    return E_OUTOFMEMORY;
}

HRESULT TE_StateQuery(const char* key, StateValue* out_val)
{
    if (!key || !out_val) return E_POINTER;
    if (key[0] == '\0') return E_INVALIDARG;
    if (strnlen_s(key, TE_STATE_KEY_MAX_LEN) == TE_STATE_KEY_MAX_LEN) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);

    AcquireSRWLockShared(&g_lock);
    if (!g_initialized) {
        ReleaseSRWLockShared(&g_lock);
        return E_UNEXPECTED;
    }

    uint32_t hash = HashFnv1a(key);
    uint32_t start = hash % TE_STATE_STORE_MAX_ENTRIES;
    uint32_t idx = start;

    do {
        if (g_entries[idx].occupied && strcmp(g_entries[idx].key, key) == 0) {
            *out_val = g_entries[idx].val;
            ReleaseSRWLockShared(&g_lock);
            return S_OK;
        }
        if (!g_entries[idx].occupied) {
            break;
        }
        idx = (idx + 1) % TE_STATE_STORE_MAX_ENTRIES;
    } while (idx != start);

    ReleaseSRWLockShared(&g_lock);
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
