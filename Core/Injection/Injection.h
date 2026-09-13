#pragma once

#include <windows.h>
#include <string>

namespace TaskbarEngine {
namespace Core {
namespace Injection {

class ProcessInjector {
public:
    static bool Inject(DWORD processId, const std::wstring& payloadDllPath);
    static bool Uninject(DWORD processId, const std::wstring& moduleName);
    
private:
    static bool VerifyProcessIntegrity(DWORD processId);
};

class HookEngine {
public:
    static bool Initialize();
    static bool Shutdown();
    static bool CreateHook(void* targetAddress, void* hookFunction, void** originalFunction);
    static bool RemoveHook(void* targetAddress);
    static bool EnableHook(void* targetAddress);
    static bool DisableHook(void* targetAddress);
};

} // namespace Injection
} // namespace Core
} // namespace TaskbarEngine
