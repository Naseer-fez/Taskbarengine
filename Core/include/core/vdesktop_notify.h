#pragma once

#include "core/event_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_VDesktopNotifyStart(TE_EventEntry* event_table, uint32_t* sub_count);
void TE_VDesktopNotifyStop(void);
void TE_VDesktopNotifyDispatchTest(const GUID* desktop_id);

#ifdef __cplusplus
}
#endif
