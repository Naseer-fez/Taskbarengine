#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_IpcServerStart(void);
void TE_IpcServerStop(void);

#ifdef __cplusplus
}
#endif
