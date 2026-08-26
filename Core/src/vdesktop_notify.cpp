#include "core/vdesktop_notify.h"
#include <core/event_dispatch.h>
#include <sdk/te_events.h>
#include <shobjidl_core.h>

extern "C" HRESULT TE_VDesktopInit(void) {
    // Phase 3 Stub: full IVirtualDesktopNotification requires undocumented COM interfaces
    return TE_S_OK;
}

extern "C" void TE_VDesktopShutdown(void) {
}
