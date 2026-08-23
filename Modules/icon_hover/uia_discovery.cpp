#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include "uia_discovery.h"
#include <wrl/client.h>
#include <sdk/te_log.h>
#include <sdk/te_debug_trace.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

static ComPtr<IUIAutomation> g_uia;
static int g_uia_com_initialized = 0;

HRESULT TE_UiaInit(void)
{
    TE_DebugTrace("[TE-DBG] UIA: TE_UiaInit entering\n");
    HRESULT hr_com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    TE_DebugTraceFmt("[TE-DBG] UIA: CoInitializeEx returned hr=0x%08X\n", (unsigned int)hr_com);
    if (SUCCEEDED(hr_com)) {
        g_uia_com_initialized++;
    } else if (hr_com != RPC_E_CHANGED_MODE) {
        TE_DebugTraceFmt("[TE-DBG] UIA: CoInitializeEx failed hr=0x%08X\n", (unsigned int)hr_com);
        return hr_com;
    }
    
    return S_OK;
}

HRESULT TE_UiaDiscoverIcons(HWND taskbar_hwnd, TE_TaskbarIcon* out_icons, int max_count, int* out_count)
{
    TE_DebugTraceFmt("[TE-DBG] UIA: DiscoverIcons entering taskbar=0x%p\n", (void*)taskbar_hwnd);
    if (!taskbar_hwnd || !IsWindow(taskbar_hwnd) || !out_icons || !out_count) return E_INVALIDARG;
    if (max_count <= 0) return E_INVALIDARG;
    *out_count = 0;

    if (!g_uia) {
        TE_DebugTrace("[TE-DBG] UIA: Creating CUIAutomation8\n");
        HRESULT hr = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_uia));
        TE_DebugTraceFmt("[TE-DBG] UIA: CUIAutomation8 returned hr=0x%08X\n", (unsigned int)hr);
        if (FAILED(hr)) {
            hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_uia));
            TE_DebugTraceFmt("[TE-DBG] UIA: CUIAutomation fallback returned hr=0x%08X\n", (unsigned int)hr);
        }
        if (FAILED(hr)) {
            TE_LogWrite(TE_LOG_ERROR, "Failed to create IUIAutomation (hr=0x%08X)", hr);
            return hr;
        }
    }

    ComPtr<IUIAutomationElement> taskbar_el;
    HRESULT hr = g_uia->ElementFromHandle(taskbar_hwnd, &taskbar_el);
    TE_DebugTraceFmt("[TE-DBG] UIA: ElementFromHandle returned hr=0x%08X element=0x%p\n", (unsigned int)hr, taskbar_el.Get());
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
    TE_DebugTraceFmt("[TE-DBG] UIA: FindAll buttons returned hr=0x%08X array=0x%p\n", (unsigned int)hr, array.Get());
    if (FAILED(hr) || !array) {
        TE_LogWrite(TE_LOG_WARN, "UIA found no buttons");
        return hr;
    }

    int length = 0;
    array->get_Length(&length);
    TE_DebugTraceFmt("[TE-DBG] UIA: Button array length=%d\n", length);

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

            if (id_bstr) {
                // Filter out system control buttons
                if (wcscmp(id_bstr, L"StartButton") == 0 ||
                    wcscmp(id_bstr, L"SearchButton") == 0 ||
                    wcscmp(id_bstr, L"TaskViewButton") == 0 ||
                    wcscmp(id_bstr, L"WidgetsButton") == 0 ||
                    wcscmp(id_bstr, L"ShowDesktopButton") == 0 ||
                    wcscmp(id_bstr, L"NotificationCenterButton") == 0 ||
                    wcscmp(id_bstr, L"SystemClockChangeNotificationsButton") == 0) {
                    SysFreeString(id_bstr);
                    continue;
                }

                out_icons[count].bounds = rect;
                out_icons[count].icon_index = count;
                wcsncpy(out_icons[count].app_id, id_bstr, 255);
                out_icons[count].app_id[255] = L'\0';
                SysFreeString(id_bstr);
                count++;
            }
        }
    }

    // Sort icons strictly left-to-right by screen coordinate
    if (count > 1) {
        std::sort(out_icons, out_icons + count, [](const TE_TaskbarIcon& a, const TE_TaskbarIcon& b) {
            return a.bounds.left < b.bounds.left;
        });
        for (int i = 0; i < count; i++) {
            out_icons[i].icon_index = i;
        }
    }

    *out_count = count;
    TE_DebugTraceFmt("[TE-DBG] UIA: DiscoverIcons leaving count=%d\n", count);
    return S_OK;
}

void TE_UiaCleanup(void)
{
    TE_DebugTrace("[TE-DBG] UIA: Cleanup resetting UIA pointer\n");
    g_uia.Reset();
    while (g_uia_com_initialized > 0) {
        CoUninitialize();
        g_uia_com_initialized--;
    }
}
