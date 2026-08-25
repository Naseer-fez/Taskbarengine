#pragma once
#include <sdk/te_types.h>
#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Publish a key-value pair to the inter-plugin state store.
 * STUB: Returns TE_E_NOTIMPL. Real implementation in Phase 4.
 * @param key    State key string.
 * @param value  Pointer to StateValue to store.
 * @return TE_E_NOTIMPL (stub).
 */
HRESULT TE_StatePublish(const char* key, const StateValue* value);

/**
 * Query a key-value pair from the inter-plugin state store.
 * STUB: Returns TE_E_NOTIMPL. Real implementation in Phase 4.
 * @param key       State key string.
 * @param out_value Pointer to receive the StateValue.
 * @return TE_E_NOTIMPL (stub).
 */
HRESULT TE_StateQuery(const char* key, StateValue* out_value);

#ifdef __cplusplus
}
#endif
