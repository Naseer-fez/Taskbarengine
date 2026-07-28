#include "core/vdesktop_notify.h"
#include <windows.h>

static TE_EventEntry* g_event_table = nullptr;
static uint32_t* g_sub_count = nullptr;
static bool g_com_initialized = false;

extern "C" HRESULT TE_VDesktopNotifyStart(TE_EventEntry* event_table, uint32_t* sub_count)
{
    if (!event_table || !sub_count) return E_POINTER;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        g_com_initialized = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        return hr;
    }

    g_event_table = event_table;
    g_sub_count = sub_count;
    return S_OK;
}

extern "C" void TE_VDesktopNotifyStop(void)
{
    g_event_table = nullptr;
    g_sub_count = nullptr;
    if (g_com_initialized) {
        CoUninitialize();
        g_com_initialized = false;
    }
}

extern "C" void TE_VDesktopNotifyDispatchTest(const GUID* desktop_id)
{
    if (!g_event_table || !g_sub_count || !desktop_id) return;
    TE_VDesktopEvent evt = { *desktop_id };
    TE_EventDispatch(g_event_table, *g_sub_count, TE_EVENT_VDESKTOP, &evt);
}
