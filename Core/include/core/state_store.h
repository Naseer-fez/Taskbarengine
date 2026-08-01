#pragma once

#include <sdk/te_types.h>
#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_STATE_STORE_MAX_ENTRIES 256
#define TE_STATE_KEY_MAX_LEN 128

/**
 * @brief Initialize the shared state store hash map and lock.
 * @return S_OK on success.
 */
HRESULT TE_StateStoreInit(void);

/**
 * @brief Shutdown the shared state store and release resources.
 */
void TE_StateStoreShutdown(void);

/**
 * @brief Publish or update a key in the state store.
 * @param key Null-terminated key string (e.g. "taskbar_resize.height").
 * @param val Pointer to StateValue to copy.
 * @return S_OK on success, E_POINTER on NULL args, E_OUTOFMEMORY if full.
 */
HRESULT TE_StatePublish(const char* key, const StateValue* val);

/**
 * @brief Query a value from the state store by key.
 * @param key Null-terminated key string.
 * @param out_val Pointer to receive copied StateValue.
 * @return S_OK on success, HRESULT_FROM_WIN32(ERROR_NOT_FOUND) if key missing.
 */
HRESULT TE_StateQuery(const char* key, StateValue* out_val);

#ifdef __cplusplus
}
#endif
