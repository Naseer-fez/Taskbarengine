#pragma once
#include <windows.h>
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_VDesktopInit(void);
void TE_VDesktopShutdown(void);

#ifdef __cplusplus
}
#endif
