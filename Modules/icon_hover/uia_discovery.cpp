#include "uia_discovery.h"
#include <uiautomation.h>
#include <wrl/client.h>
#include <sdk/te_log.h>

using Microsoft::WRL::ComPtr;

static ComPtr<IUIAutomation> g_uia;
static bool g_uia_com_initialized = false;

HRESULT TE_UiaInit(void)
{
    HRESULT hr_com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr_com) || hr_com == RPC_E_CHANGED_MODE) {
        g_uia_com_initialized = SUCCEEDED(hr_com);
    } else {
        return hr_com;
    }
    return S_OK;
}

HRESULT TE_UiaDiscoverIcons(HWND taskbar_hwnd, TE_TaskbarIcon* out_icons, int max_count, int* out_count)
{
    if (!out_icons || !out_count) return E_POINTER;
    *out_count = 0;

    if (!g_uia) {
        HRESULT hr = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_uia));
        if (FAILED(hr)) {
            hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_uia));
        }
        if (FAILED(hr)) {
            TE_LogWrite(TE_LOG_ERROR, "Failed to create IUIAutomation (hr=0x%08X)", hr);
            return hr;
        }
    }

    ComPtr<IUIAutomationElement> taskbar_el;
    HRESULT hr = g_uia->ElementFromHandle(taskbar_hwnd, &taskbar_el);
    if (FAILED(hr) || !taskbar_el) {
        TE_LogWrite(TE_LOG_ERROR, "UIA failed to get taskbar element");
        return hr;
    }

    ComPtr<IUIAutomationCondition> condition;
    VARIANT var;
    var.vt = VT_I4;
    var.lVal = UIA_ButtonControlTypeId;
    hr = g_uia->CreatePropertyCondition(UIA_ControlTypePropertyId, var, &condition);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IUIAutomationElementArray> array;
    hr = taskbar_el->FindAll(TreeScope_Descendants, condition.Get(), &array);
    if (FAILED(hr) || !array) {
        TE_LogWrite(TE_LOG_WARN, "UIA found no buttons");
        return hr;
    }

    int length = 0;
    array->get_Length(&length);

    int count = 0;
    for (int i = 0; i < length && count < max_count; i++) {
        ComPtr<IUIAutomationElement> el;
        if (SUCCEEDED(array->GetElement(i, &el)) && el) {
            RECT rect = {};
            el->get_CurrentBoundingRectangle(&rect);
            
            // Heuristic to ignore non-icon buttons
            if (rect.right - rect.left < 20 || rect.bottom - rect.top < 20) {
                continue;
            }

            BSTR id_bstr = nullptr;
            el->get_CurrentAutomationId(&id_bstr);
            if (!id_bstr || wcslen(id_bstr) == 0) {
                if (id_bstr) SysFreeString(id_bstr);
                el->get_CurrentName(&id_bstr);
            }

            out_icons[count].bounds = rect;
            out_icons[count].icon_index = count;
            if (id_bstr) {
                wcsncpy(out_icons[count].app_id, id_bstr, 255);
                out_icons[count].app_id[255] = L'\0';
                SysFreeString(id_bstr);
            } else {
                out_icons[count].app_id[0] = L'\0';
            }

            count++;
        }
    }

    *out_count = count;
    return S_OK;
}

void TE_UiaCleanup(void)
{
    g_uia.Reset();
    if (g_uia_com_initialized) {
        CoUninitialize();
        g_uia_com_initialized = false;
    }
}
