#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <algorithm>

extern "C" {
#include <sdk/te_types.h>
#include <sdk/te_plugin.h>

#define TE_MAX_PLUGINS 32
typedef struct TE_PluginEntry {
    HMODULE module_handle;
    const PluginInterface* interface_ptr;
    const PluginMetadata* metadata;
    uint32_t plugin_id;
    BOOL enabled;
    int fault_count;
    BOOL initialized;
    wchar_t dll_path[MAX_PATH];
} TE_PluginEntry;

int TE_PluginLoaderGetCount(void);
TE_PluginEntry* TE_PluginLoaderGetEntry(int index);
TE_PluginEntry* TE_PluginLoaderFindByName(const char* name);
HRESULT TE_PluginLoaderInit(void);
void TE_PluginLoaderShutdown(void);
}

TEST_CASE("Plugin loader initialization", "[plugins]") {
    TE_PluginLoaderInit();

    SECTION("Initial state") {
        CHECK(TE_PluginLoaderGetCount() == 0);
        CHECK(TE_PluginLoaderGetEntry(0) == nullptr);
        CHECK(TE_PluginLoaderGetEntry(-1) == nullptr);
    }

    SECTION("Find by name returns null when empty") {
        CHECK(TE_PluginLoaderFindByName("nonexistent") == nullptr);
        CHECK(TE_PluginLoaderFindByName(nullptr) == nullptr);
    }

    SECTION("Max plugins constant") {
        CHECK(TE_MAX_PLUGINS == 32);
    }

    TE_PluginLoaderShutdown();
}
