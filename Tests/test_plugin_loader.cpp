#include <catch2/catch_test_macros.hpp>
#include <core/plugin_loader.h>
#include <windows.h>
#include <cstring>

#ifndef TE_TEST_MODULES_DIR
#define TE_TEST_MODULES_DIR L"Modules"
#endif

extern "C" {
    static uint32_t g_test_plugin_id = 0;
    uint32_t TE_CoreManagerGetCurrentPluginId(void) { return g_test_plugin_id; }
    void TE_CoreManagerSetCurrentPluginId(uint32_t id) { g_test_plugin_id = id; }
}


TEST_CASE("Plugin Loader Scan and Lifecycle", "[plugin]") {
    TE_PluginEntry registry[TE_MAX_PLUGINS];
    uint32_t count = 0;

    SECTION("Scan Modules directory and check plugins") {
        HRESULT hr = TE_PluginLoaderScan(TE_TEST_MODULES_DIR, registry, &count);
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(count >= 1);

        TE_PluginEntry* dummy = nullptr;
        TE_PluginEntry* fault = nullptr;

        for (uint32_t i = 0; i < count; i++) {
            if (registry[i].metadata && registry[i].metadata->name) {
                if (strcmp(registry[i].metadata->name, "DummyPlugin") == 0) {
                    dummy = &registry[i];
                } else if (strcmp(registry[i].metadata->name, "FaultPlugin") == 0) {
                    fault = &registry[i];
                }
            }
        }

        /* Test DummyPlugin Happy Path */
        REQUIRE(dummy != nullptr);
        REQUIRE(dummy->iface != nullptr);
        REQUIRE(dummy->metadata != nullptr);

        if (dummy->iface->Initialize) {
            dummy->iface->Initialize(dummy->context);
        }

        HRESULT en_hr = TE_PluginLoaderEnable(dummy);
        REQUIRE(SUCCEEDED(en_hr));
        REQUIRE(dummy->enabled == true);

        HRESULT dis_hr = TE_PluginLoaderDisable(dummy);
        REQUIRE(SUCCEEDED(dis_hr));
        REQUIRE(dummy->enabled == false);

        /* Verify Disable is permitted even if disabled_by_fault is true */
        dummy->enabled = true;
        dummy->disabled_by_fault = true;
        HRESULT fault_dis_hr = TE_PluginLoaderDisable(dummy);
        REQUIRE(SUCCEEDED(fault_dis_hr));
        REQUIRE(dummy->enabled == false);


        /* Test FaultPlugin Isolation Path under MSVC SEH */
#ifdef _MSC_VER
        if (fault != nullptr && fault->iface != nullptr) {
            if (fault->iface->Initialize) {
                fault->iface->Initialize(fault->context);
            }
            HRESULT fault_en = TE_PluginLoaderEnable(fault);
            REQUIRE(FAILED(fault_en));
            REQUIRE(fault->enabled == false);
        }
#else
        (void)fault;
#endif

        TE_PluginLoaderUnloadAll(registry, count);
    }
}
