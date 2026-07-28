#include "core/power_device.h"
#include <dbt.h>

static TE_EventEntry* g_event_table = NULL;
static uint32_t* g_sub_count = NULL;
static HPOWERNOTIFY g_power_notify = NULL;
static HDEVNOTIFY g_device_notify = NULL;

HRESULT TE_PowerDeviceStart(HWND hwnd, TE_EventEntry* event_table, uint32_t* sub_count)
{
    if (!hwnd || !event_table || !sub_count) return E_POINTER;
    g_event_table = event_table;
    g_sub_count = sub_count;

    g_power_notify = RegisterPowerSettingNotification(hwnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);

    DEV_BROADCAST_DEVICEINTERFACE_W filter;
    ZeroMemory(&filter, sizeof(filter));
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    g_device_notify = RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    return S_OK;
}

void TE_PowerDeviceStop(void)
{
    if (g_power_notify) {
        UnregisterPowerSettingNotification(g_power_notify);
        g_power_notify = NULL;
    }
    if (g_device_notify) {
        UnregisterDeviceNotification(g_device_notify);
        g_device_notify = NULL;
    }
    g_event_table = NULL;
    g_sub_count = NULL;
}

bool TE_PowerDeviceHandleMessage(UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (!g_event_table || !g_sub_count) return false;

    if (msg == WM_POWERBROADCAST) {
        TE_PowerEvent evt = {
            .event_type = (DWORD)wparam,
            .event_data = (void*)lparam
        };
        TE_EventDispatch(g_event_table, *g_sub_count, TE_EVENT_POWER, &evt);
        return true;
    }

    if (msg == WM_DEVICECHANGE) {
        TE_DeviceEvent evt = {
            .event_type = (DWORD)wparam,
            .event_data = (void*)lparam
        };
        TE_EventDispatch(g_event_table, *g_sub_count, TE_EVENT_DEVICE, &evt);
        return true;
    }

    return false;
}
