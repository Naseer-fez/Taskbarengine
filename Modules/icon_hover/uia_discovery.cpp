#include "uia_discovery.h"
#include <uiautomation.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

static ComPtr<IUIAutomation> g_automation = nullptr;

static HRESULT EnsureAutomation(void)
{
    if (g_automation) return S_OK;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        /* Already initialized in multithreaded mode or similar is fine */
    }

    hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_automation));
    return hr;
}

extern "C" void TE_UiaCleanup(void)
{
    g_automation.Reset();
}

extern "C" HRESULT TE_UiaDiscoverIcons(HWND taskbar_hwnd, TE_TaskbarIcon* out_icons, int max_count, int* out_count)
{
    if (!out_icons || max_count <= 0 || !out_count) return E_POINTER;
    *out_count = 0;

    if (!taskbar_hwnd || !IsWindow(taskbar_hwnd)) return E_HANDLE;

    HRESULT hr = EnsureAutomation();
    if (FAILED(hr) || !g_automation) return hr;

    ComPtr<IUIAutomationElement> root_element;
    hr = g_automation->ElementFromHandle(taskbar_hwnd, &root_element);
    if (FAILED(hr) || !root_element) return hr;

    /* Condition: ControlType == Button */
    VARIANT var_prop;
    var_prop.vt = VT_I4;
    var_prop.lVal = UIA_ButtonControlTypeId;

    ComPtr<IUIAutomationCondition> condition;
    hr = g_automation->CreatePropertyCondition(UIA_ControlTypePropertyId, var_prop, &condition);
    if (FAILED(hr) || !condition) return hr;

    ComPtr<IUIAutomationElementArray> element_array;
    hr = root_element->FindAll(TreeScope_Children, condition.Get(), &element_array);
    if (FAILED(hr) || !element_array) return hr;

    int length = 0;
    element_array->get_Length(&length);

    HMONITOR monitor = MonitorFromWindow(taskbar_hwnd, MONITOR_DEFAULTTONEAREST);
    int count = 0;

    for (int i = 0; i < length && count < max_count; i++) {
        ComPtr<IUIAutomationElement> elem;
        if (FAILED(element_array->GetElement(i, &elem)) || !elem) continue;

        RECT rect;
        if (FAILED(elem->get_CurrentBoundingRectangle(&rect))) continue;

        /* Filter invalid or zero size elements */
        if (rect.right <= rect.left || rect.bottom <= rect.top) continue;

        BSTR auto_id = NULL;
        elem->get_CurrentAutomationId(&auto_id);

        BSTR name = NULL;
        if (!auto_id || ::SysStringLen(auto_id) == 0) {
            elem->get_CurrentName(&name);
        }

        TE_TaskbarIcon* icon = &out_icons[count];
        icon->bounds.left = rect.left;
        icon->bounds.top = rect.top;
        icon->bounds.right = rect.right;
        icon->bounds.bottom = rect.bottom;
        icon->icon_index = count;
        icon->monitor = monitor;

        const wchar_t* src_str = (auto_id && ::SysStringLen(auto_id) > 0) ? auto_id : (name ? name : L"");
        wcsncpy_s(icon->app_id, 256, src_str, _TRUNCATE);

        if (auto_id) ::SysFreeString(auto_id);
        if (name) ::SysFreeString(name);

        count++;
    }

    *out_count = count;
    return S_OK;
}
