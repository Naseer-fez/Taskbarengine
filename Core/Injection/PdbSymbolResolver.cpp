#include "PdbSymbolResolver.h"
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#include <iostream>

#pragma comment(lib, "dbghelp.lib")

namespace TaskbarEngine {
namespace Core {
namespace Injection {

PdbSymbolResolver::PdbSymbolResolver() {
    SymSetOptions(SYMOPT_DEBUG | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_AUTO_PUBLICS);
}

PdbSymbolResolver::~PdbSymbolResolver() {
    SymCleanup(GetCurrentProcess());
}

bool PdbSymbolResolver::EnsureCacheDirectory() {
    if (m_cacheDir.empty()) {
        wchar_t appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData))) {
            m_cacheDir = std::wstring(appData) + L"\\TaskbarEngine\\Symbols";
        } else {
            return false;
        }
    }
    CreateDirectoryW(m_cacheDir.c_str(), NULL); // Ensure directory exists
    return true;
}

bool PdbSymbolResolver::Initialize(const std::wstring& cacheDir) {
    m_cacheDir = cacheDir;
    if (!EnsureCacheDirectory()) return false;

    std::wstring searchPath = L"srv*" + m_cacheDir + L"*https://msdl.microsoft.com/download/symbols";
    
    // Initialize dbghelp for current process
    if (!SymInitializeW(GetCurrentProcess(), searchPath.c_str(), FALSE)) {
        return false;
    }

    return true;
}

bool PdbSymbolResolver::DownloadSymbolsForModule(const std::wstring& modulePath) {
    HANDLE hProcess = GetCurrentProcess();
    DWORD64 baseAddr = SymLoadModuleExW(hProcess, NULL, modulePath.c_str(), NULL, 0, 0, NULL, 0);
    if (baseAddr == 0) {
        return false;
    }
    // SymLoadModuleExW will automatically download missing symbols to cache
    return true;
}

void* PdbSymbolResolver::ResolveSymbol(const std::wstring& modulePath, const std::string& symbolName) {
    HANDLE hProcess = GetCurrentProcess();
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    if (SymFromName(hProcess, symbolName.c_str(), symbol)) {
        void* addr = (void*)symbol->Address;
        free(symbol);
        return addr;
    }
    
    free(symbol);
    return nullptr;
}

} // namespace Injection
} // namespace Core
} // namespace TaskbarEngine
