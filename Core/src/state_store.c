#include "core/state_store.h"
#include <sdk/te_log.h>

HRESULT TE_StatePublish(const char* key, const StateValue* value)
{
    (void)key;
    (void)value;
    TE_LogWrite(TE_LOG_DEBUG, "StateStore", "PublishState called (stub — not implemented until Phase 4)");
    return TE_E_NOTIMPL;
}

HRESULT TE_StateQuery(const char* key, StateValue* out_value)
{
    (void)key;
    (void)out_value;
    TE_LogWrite(TE_LOG_DEBUG, "StateStore", "QueryState called (stub — not implemented until Phase 4)");
    return TE_E_NOTIMPL;
}
