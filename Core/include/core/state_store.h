#pragma once
#include <sdk/te_types.h>
#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the global state store.
 * Zeroes out the storage and initializes synchronization primitives.
 * @return TE_S_OK on success.
 */
HRESULT TE_StateStoreInit(void);

/**
 * @brief Shuts down the global state store.
 * Cleans up resources and clears the table.
 */
void TE_StateStoreShutdown(void);

/**
 * @brief Publishes a value to the shared state store.
 * Updates an existing entry if the key matches, or creates a new entry.
 * Uses a thread-safe SRWLock-protected hash map.
 * @param key The unique string key (max 63 characters).
 * @param value The value to store.
 * @return TE_S_OK on success, TE_E_INVALIDARG on bad arguments, TE_E_OUTOFMEMORY if full.
 */
HRESULT TE_StatePublish(const char* key, const StateValue* value);

/**
 * @brief Queries a value from the shared state store.
 * Thread-safe read operation.
 * @param key The key to look up.
 * @param out_value Pointer to receive the retrieved value.
 * @return TE_S_OK on success, TE_E_FAIL if not found, TE_E_INVALIDARG on bad arguments.
 */
HRESULT TE_StateQuery(const char* key, StateValue* out_value);

#ifdef __cplusplus
}
#endif
