#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TE_CoreState TE_CoreState;

HRESULT TE_CoreManagerInit(HINSTANCE hinstance);
void TE_CoreManagerShutdown(void);
void TE_CoreManagerOnConfigChanged(void* core_state_ptr);

#ifdef __cplusplus
}
#endif
