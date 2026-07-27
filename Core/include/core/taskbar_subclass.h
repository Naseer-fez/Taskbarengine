#pragma once

#include <sdk/te_types.h>
#include "core/event_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_TaskbarSubclassInstall(HWND taskbar_hwnd, TE_EventEntry* event_table, uint32_t* sub_count, void* core_state_ptr);
void TE_TaskbarSubclassRemove(HWND taskbar_hwnd);

#ifdef __cplusplus
}
#endif
