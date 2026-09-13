#pragma once

#include <string>
#include <vector>

namespace TaskbarEngine {
namespace Core {
namespace Injection {

class PdbSymbolResolver {
public:
    PdbSymbolResolver();
    ~PdbSymbolResolver();

    bool Initialize(const std::wstring& cacheDir);
    bool DownloadSymbolsForModule(const std::wstring& modulePath);
    void* ResolveSymbol(const std::wstring& modulePath, const std::string& symbolName);

private:
    std::wstring m_cacheDir;
    bool EnsureCacheDirectory();
};

} // namespace Injection
} // namespace Core
} // namespace TaskbarEngine
