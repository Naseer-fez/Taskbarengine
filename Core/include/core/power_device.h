#pragma once

#include "core/event_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_PowerDeviceStart(HWND hwnd, TE_EventEntry* event_table, uint32_t* sub_count);
void TE_PowerDeviceStop(void);
bool TE_PowerDeviceHandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

#ifdef __cplusplus
}
#endif
