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

struct ThreadData {
    TE_PluginMethodVoid method;
    HRESULT hr;
    const char* plugin_name;
    int exception_occurred;
};

static DWORD WINAPI PluginThreadProc(LPVOID lpParam) {
    struct ThreadData* data = (struct ThreadData*)lpParam;
    __try {
        data->hr = data->method();
    } __except(TE_FaultFilter(GetExceptionInformation(), data->plugin_name)) {
        data->exception_occurred = 1;
        data->hr = TE_E_FAIL;
    }
    return 0;
}

HRESULT TE_FaultIsolatedCall(int* fault_count, const char* plugin_name,
                              const char* method_name, TE_PluginMethodVoid method)
{
    if (!method) return TE_E_INVALIDARG;
    
    struct ThreadData data = { method, TE_S_OK, plugin_name, 0 };
    HANDLE hThread = CreateThread(NULL, 0, PluginThreadProc, &data, 0, NULL);
    if (!hThread) return TE_E_FAIL;
    
    // Enforce a strict 100ms timeout for any plugin lifecycle call
    DWORD waitRes = WaitForSingleObject(hThread, 100);
    if (waitRes == WAIT_TIMEOUT) {
        if (fault_count) *fault_count += 100; // Quarantine plugin by pushing faults over threshold
        
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Plugin '%s' TIMED OUT during '%s'. Quarantined.", 
                    plugin_name ? plugin_name : "unknown", 
                    method_name ? method_name : "unknown");
        TE_LogWrite(TE_LOG_ERROR, "FaultIsolation", buf);
        
        // Detach and abandon the hanging thread to unblock the main engine thread
        CloseHandle(hThread);
        return TE_E_FAIL;
    }
    
    CloseHandle(hThread);
    
    if (data.exception_occurred) {
        if (fault_count) (*fault_count)++;
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Fault in plugin '%s' during '%s'. Total faults: %d", 
                    plugin_name ? plugin_name : "unknown", 
                    method_name ? method_name : "unknown", 
                    fault_count ? *fault_count : 1);
        TE_LogWrite(TE_LOG_ERROR, "FaultIsolation", buf);
    }
    
    return data.hr;
}
