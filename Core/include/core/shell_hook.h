#pragma once

#include "core/event_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_ShellHookStart(HWND hwnd, TE_EventEntry* event_table, uint32_t* sub_count);
void TE_ShellHookStop(HWND hwnd);
bool TE_ShellHookHandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

#ifdef __cplusplus
}
#endif
