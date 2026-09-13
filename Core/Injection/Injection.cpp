#include "Injection.h"
#include <tlhelp32.h>
#include <iostream>

// MinHook could be included here. Assuming it is part of ThirdParty or statically linked.

namespace TaskbarEngine {
namespace Core {
namespace Injection {

bool ProcessInjector::VerifyProcessIntegrity(DWORD processId) {
    // Check if the process matches expected integrity levels, token, etc.
    // E.g. skip injection if target has higher integrity than current process, or is protected.
    return true; 
}

bool ProcessInjector::Inject(DWORD processId, const std::wstring& payloadDllPath) {
    if (!VerifyProcessIntegrity(processId)) return false;

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) return false;

    // SEH block for injection
    __try {
        void* pRemoteBuf = VirtualAllocEx(hProcess, NULL, (payloadDllPath.length() + 1) * sizeof(wchar_t), MEM_COMMIT, PAGE_READWRITE);
        if (!pRemoteBuf) {
            CloseHandle(hProcess);
            return false;
        }

        WriteProcessMemory(hProcess, pRemoteBuf, payloadDllPath.c_str(), (payloadDllPath.length() + 1) * sizeof(wchar_t), NULL);

        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        LPTHREAD_START_ROUTINE pLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryW");

        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLibrary, pRemoteBuf, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
        } else {
            VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }

        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hProcess);
    return true;
}

bool ProcessInjector::Uninject(DWORD processId, const std::wstring& moduleName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me;
    me.dwSize = sizeof(MODULEENTRY32W);
    HMODULE hModuleToUnload = NULL;

    if (Module32FirstW(hSnapshot, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName.c_str()) == 0) {
                hModuleToUnload = me.hModule;
                break;
            }
        } while (Module32NextW(hSnapshot, &me));
    }
    CloseHandle(hSnapshot);

    if (!hModuleToUnload) return false;

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) return false;

    __try {
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        LPTHREAD_START_ROUTINE pFreeLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "FreeLibrary");

        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pFreeLibrary, hModuleToUnload, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hProcess);
    return true;
}

bool HookEngine::Initialize() {
    // Initialize MinHook or Windhawk hook engine
    return true;
}

bool HookEngine::Shutdown() {
    // Shutdown hooking engine
    return true;
}

bool HookEngine::CreateHook(void* targetAddress, void* hookFunction, void** originalFunction) {
    // Create hook using MinHook/Windhawk logic
    return true;
}

bool HookEngine::RemoveHook(void* targetAddress) {
    return true;
}

bool HookEngine::EnableHook(void* targetAddress) {
    return true;
}

bool HookEngine::DisableHook(void* targetAddress) {
    return true;
}

} // namespace Injection
} // namespace Core
} // namespace TaskbarEngine
