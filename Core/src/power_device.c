#include "core/power_device.h"
#include "core/event_dispatch.h"
#include <sdk/te_events.h>
#include <dbt.h>

void TE_PowerProcess(WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    if (wParam == PBT_APMSUSPEND) {
        TE_PowerData data = { TRUE };
        TE_EventDispatchFire(TE_EVENT_POWER, &data);
    } else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
        TE_PowerData data = { FALSE };
        TE_EventDispatchFire(TE_EVENT_POWER, &data);
    }
}

void TE_DeviceProcess(WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    if (wParam == DBT_DEVICEARRIVAL) {
        TE_DeviceData data = { TRUE };
        TE_EventDispatchFire(TE_EVENT_DEVICE, &data);
    } else if (wParam == DBT_DEVICEREMOVECOMPLETE) {
        TE_DeviceData data = { FALSE };
        TE_EventDispatchFire(TE_EVENT_DEVICE, &data);
    }
}
