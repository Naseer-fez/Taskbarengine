#include "scheduler.h"
#include <taskschd.h>
#include <comdef.h>

static const wchar_t* TASK_NAME = L"TaskbarEngine_Logon";

HRESULT TE_SchedulerRegisterTask(const wchar_t* exe_path)
{
    if (!exe_path) return E_POINTER;

    ITaskService* pService = NULL;
    ITaskFolder* pRootFolder = NULL;
    ITaskDefinition* pTask = NULL;
    IRegistrationInfo* pRegInfo = NULL;
    IPrincipal* pPrincipal = NULL;
    ITaskSettings* pSettings = NULL;
    ITriggerCollection* pTriggerCollection = NULL;
    ITrigger* pTrigger = NULL;
    ILogonTrigger* pLogonTrigger = NULL;
    IActionCollection* pActionCollection = NULL;
    IAction* pAction = NULL;
    IExecAction* pExecAction = NULL;
    IRegisteredTask* pRegisteredTask = NULL;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool co_init = SUCCEEDED(hr);
    
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) goto cleanup;

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) goto cleanup;

    hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) goto cleanup;

    pRootFolder->DeleteTask(_bstr_t(TASK_NAME), 0);

    hr = pService->NewTask(0, &pTask);
    if (FAILED(hr)) {
        pRootFolder->Release();
        pRootFolder = NULL;
        goto cleanup;
    }

    // Set registration info
    hr = pTask->get_RegistrationInfo(&pRegInfo);
    if (SUCCEEDED(hr) && pRegInfo) {
        pRegInfo->put_Author(_bstr_t(L"TaskbarEngine"));
        pRegInfo->Release();
        pRegInfo = NULL;
    }

    // Set principal to logon user, highest privileges
    hr = pTask->get_Principal(&pPrincipal);
    if (SUCCEEDED(hr) && pPrincipal) {
        pPrincipal->put_Id(_bstr_t(L"Author"));
        pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        pPrincipal->Release();
        pPrincipal = NULL;
    }

    // Set settings (don't stop on idle, etc)
    hr = pTask->get_Settings(&pSettings);
    if (SUCCEEDED(hr) && pSettings) {
        pSettings->put_StartWhenAvailable(VARIANT_TRUE);
        pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")); // 0 = no limit
        pSettings->Release();
        pSettings = NULL;
    }

    // Add logon trigger
    hr = pTask->get_Triggers(&pTriggerCollection);
    if (SUCCEEDED(hr) && pTriggerCollection) {
        hr = pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger);
        if (SUCCEEDED(hr) && pTrigger) {
            hr = pTrigger->QueryInterface(IID_ILogonTrigger, (void**)&pLogonTrigger);
            if (SUCCEEDED(hr) && pLogonTrigger) {
                pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));
                pLogonTrigger->Release();
                pLogonTrigger = NULL;
            }
            pTrigger->Release();
            pTrigger = NULL;
        }
        pTriggerCollection->Release();
        pTriggerCollection = NULL;
    }

    // Add execute action
    hr = pTask->get_Actions(&pActionCollection);
    if (SUCCEEDED(hr) && pActionCollection) {
        hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
        if (SUCCEEDED(hr) && pAction) {
            hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
            if (SUCCEEDED(hr) && pExecAction) {
                pExecAction->put_Path(_bstr_t(exe_path));
                pExecAction->Release();
                pExecAction = NULL;
            }
            pAction->Release();
            pAction = NULL;
        }
        pActionCollection->Release();
        pActionCollection = NULL;
    }

    hr = pRootFolder->RegisterTaskDefinition(_bstr_t(TASK_NAME), pTask, TASK_CREATE_OR_UPDATE, _variant_t(), _variant_t(), TASK_LOGON_INTERACTIVE_TOKEN, _variant_t(L""), &pRegisteredTask);
    if (SUCCEEDED(hr) && pRegisteredTask) {
        pRegisteredTask->Release();
        pRegisteredTask = NULL;
    }

    if (pTask) { pTask->Release(); pTask = NULL; }
    if (pRootFolder) { pRootFolder->Release(); pRootFolder = NULL; }

cleanup:
    if (pService) pService->Release();
    if (co_init) CoUninitialize();
    
    return hr;
}

HRESULT TE_SchedulerRemoveTask(void)
{
    ITaskService* pService = NULL;
    ITaskFolder* pRootFolder = NULL;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool co_init = SUCCEEDED(hr);
    
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) goto cleanup;

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) goto cleanup;

    hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) goto cleanup;

    hr = pRootFolder->DeleteTask(_bstr_t(TASK_NAME), 0);

cleanup:
    if (pRootFolder) pRootFolder->Release();
    if (pService) pService->Release();
    if (co_init) CoUninitialize();
    
    return hr;
}
