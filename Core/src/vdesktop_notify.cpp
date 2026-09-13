#include "core/vdesktop_notify.h"
#include <core/event_dispatch.h>
#include <sdk/te_events.h>
#if defined(__has_include)
  #if __has_include(<shobjidl_core.h>)
    #include <shobjidl_core.h>
  #else
    #include <shobjidl.h>
  #endif
#else
  #include <shobjidl.h>
#endif

extern "C" HRESULT TE_VDesktopInit(void) {
    // Phase 3 Stub: full IVirtualDesktopNotification requires undocumented COM interfaces
    return TE_S_OK;
}

extern "C" void TE_VDesktopShutdown(void) {
}
