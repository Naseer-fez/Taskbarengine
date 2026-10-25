#include "core/fault_isolation.h"
#include <windows.h>
#include <stdio.h>
#include <sdk/te_log.h>

LONG TE_FaultFilter(EXCEPTION_POINTERS* ep, const char* plugin_name)
{
    char buf[256];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Plugin '%s' caused exception 0x%08X", 
                plugin_name ? plugin_name : "unknown", 
                ep->ExceptionRecord->ExceptionCode);
    TE_LogWrite(TE_LOG_ERROR, "FaultIsolation", buf);
    return EXCEPTION_EXECUTE_HANDLER;
}

HRESULT TE_FaultIsolatedCall(int* fault_count, const char* plugin_name,
                              const char* method_name, TE_PluginMethodVoid method)
{
    if (!method) return TE_E_INVALIDARG;
    
    HRESULT hr = TE_S_OK;
#ifdef _MSC_VER
    __try {
        hr = method();
    } __except(TE_FaultFilter(GetExceptionInformation(), plugin_name)) {
        if (fault_count) {
            (*fault_count)++;
        }
        
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Fault in plugin '%s' during '%s'. Total faults: %d", 
                    plugin_name ? plugin_name : "unknown", 
                    method_name ? method_name : "unknown", 
                    fault_count ? *fault_count : 1);
        TE_LogWrite(TE_LOG_ERROR, "FaultIsolation", buf);
        
        hr = TE_E_FAIL;
    }
#else
    (void)plugin_name;
    (void)method_name;
    (void)fault_count;
    hr = method();
#endif
    return hr;
}
