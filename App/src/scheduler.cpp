#include "scheduler.h"
#include <taskschd.h>
#include <comdef.h>

static const wchar_t* TASK_NAME = L"TaskbarEngine_Logon";

HRESULT TE_SchedulerRegisterTask(const wchar_t* exe_path)
{
    if (!exe_path) return E_POINTER;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool co_init = SUCCEEDED(hr);
    
    ITaskService* pService = NULL;
    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) goto cleanup;

    hr = pService->lpVtbl->Connect(pService, _variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) goto cleanup;

    ITaskFolder* pRootFolder = NULL;
    hr = pService->lpVtbl->GetFolder(pService, _bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) goto cleanup;

    pRootFolder->lpVtbl->DeleteTask(pRootFolder, _bstr_t(TASK_NAME), 0);

    ITaskDefinition* pTask = NULL;
    hr = pService->lpVtbl->NewTask(pService, 0, &pTask);
    if (FAILED(hr)) {
        pRootFolder->lpVtbl->Release(pRootFolder);
        goto cleanup;
    }

    // Set registration info
    IRegistrationInfo* pRegInfo = NULL;
    hr = pTask->lpVtbl->get_RegistrationInfo(pTask, &pRegInfo);
    if (SUCCEEDED(hr)) {
        pRegInfo->lpVtbl->put_Author(pRegInfo, _bstr_t(L"TaskbarEngine"));
        pRegInfo->lpVtbl->Release(pRegInfo);
    }

    // Set principal to logon user, highest privileges
    IPrincipal* pPrincipal = NULL;
    hr = pTask->lpVtbl->get_Principal(pTask, &pPrincipal);
    if (SUCCEEDED(hr)) {
        pPrincipal->lpVtbl->put_Id(pPrincipal, _bstr_t(L"Author"));
        pPrincipal->lpVtbl->put_LogonType(pPrincipal, TASK_LOGON_INTERACTIVE_TOKEN);
        pPrincipal->lpVtbl->put_RunLevel(pPrincipal, TASK_RUNLEVEL_HIGHEST);
        pPrincipal->lpVtbl->Release(pPrincipal);
    }

    // Set settings (don't stop on idle, etc)
    ITaskSettings* pSettings = NULL;
    hr = pTask->lpVtbl->get_Settings(pTask, &pSettings);
    if (SUCCEEDED(hr)) {
        pSettings->lpVtbl->put_StartWhenAvailable(pSettings, VARIANT_TRUE);
        pSettings->lpVtbl->put_DisallowStartIfOnBatteries(pSettings, VARIANT_FALSE);
        pSettings->lpVtbl->put_StopIfGoingOnBatteries(pSettings, VARIANT_FALSE);
        pSettings->lpVtbl->put_ExecutionTimeLimit(pSettings, _bstr_t(L"PT0S")); // 0 = no limit
        pSettings->lpVtbl->Release(pSettings);
    }

    // Add logon trigger
    ITriggerCollection* pTriggerCollection = NULL;
    hr = pTask->lpVtbl->get_Triggers(pTask, &pTriggerCollection);
    if (SUCCEEDED(hr)) {
        ITrigger* pTrigger = NULL;
        hr = pTriggerCollection->lpVtbl->Create(pTriggerCollection, TASK_TRIGGER_LOGON, &pTrigger);
        if (SUCCEEDED(hr)) {
            ILogonTrigger* pLogonTrigger = NULL;
            hr = pTrigger->lpVtbl->QueryInterface(pTrigger, &IID_ILogonTrigger, (void**)&pLogonTrigger);
            if (SUCCEEDED(hr)) {
                pLogonTrigger->lpVtbl->put_Id(pLogonTrigger, _bstr_t(L"Trigger1"));
                pLogonTrigger->lpVtbl->Release(pLogonTrigger);
            }
            pTrigger->lpVtbl->Release(pTrigger);
        }
        pTriggerCollection->lpVtbl->Release(pTriggerCollection);
    }

    // Add execute action
    IActionCollection* pActionCollection = NULL;
    hr = pTask->lpVtbl->get_Actions(pTask, &pActionCollection);
    if (SUCCEEDED(hr)) {
        IAction* pAction = NULL;
        hr = pActionCollection->lpVtbl->Create(pActionCollection, TASK_ACTION_EXEC, &pAction);
        if (SUCCEEDED(hr)) {
            IExecAction* pExecAction = NULL;
            hr = pAction->lpVtbl->QueryInterface(pAction, &IID_IExecAction, (void**)&pExecAction);
            if (SUCCEEDED(hr)) {
                pExecAction->lpVtbl->put_Path(pExecAction, _bstr_t(exe_path));
                pExecAction->lpVtbl->Release(pExecAction);
            }
            pAction->lpVtbl->Release(pAction);
        }
        pActionCollection->lpVtbl->Release(pActionCollection);
    }

    IRegisteredTask* pRegisteredTask = NULL;
    hr = pRootFolder->lpVtbl->RegisterTaskDefinition(pRootFolder, _bstr_t(TASK_NAME), pTask, TASK_CREATE_OR_UPDATE, _variant_t(), _variant_t(), TASK_LOGON_INTERACTIVE_TOKEN, _variant_t(L""), &pRegisteredTask);
    if (SUCCEEDED(hr)) {
        pRegisteredTask->lpVtbl->Release(pRegisteredTask);
    }

    pTask->lpVtbl->Release(pTask);
    pRootFolder->lpVtbl->Release(pRootFolder);

cleanup:
    if (pService) pService->lpVtbl->Release(pService);
    if (co_init) CoUninitialize();
    
    return hr;
}

HRESULT TE_SchedulerRemoveTask(void)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool co_init = SUCCEEDED(hr);
    
    ITaskService* pService = NULL;
    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) goto cleanup;

    hr = pService->lpVtbl->Connect(pService, _variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) goto cleanup;

    ITaskFolder* pRootFolder = NULL;
    hr = pService->lpVtbl->GetFolder(pService, _bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) goto cleanup;

    hr = pRootFolder->lpVtbl->DeleteTask(pRootFolder, _bstr_t(TASK_NAME), 0);
    pRootFolder->lpVtbl->Release(pRootFolder);

cleanup:
    if (pService) pService->lpVtbl->Release(pService);
    if (co_init) CoUninitialize();
    
    return hr;
}
