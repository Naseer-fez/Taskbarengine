#include <catch2/catch_test_macros.hpp>
#include <core/plugin_loader.h>
#include <windows.h>

#ifndef TE_TEST_MODULES_DIR
#define TE_TEST_MODULES_DIR L"Modules"
#endif

TEST_CASE("Plugin Loader Scan and Lifecycle", "[plugin]") {
    TE_PluginEntry registry[TE_MAX_PLUGINS];
    uint32_t count = 0;

    SECTION("Scan Modules directory") {
        HRESULT hr = TE_PluginLoaderScan(TE_TEST_MODULES_DIR, registry, &count);
        REQUIRE(SUCCEEDED(hr));

        if (count > 0) {
            REQUIRE(registry[0].interface != nullptr);
            REQUIRE(registry[0].metadata != nullptr);
            REQUIRE(registry[0].metadata->name != nullptr);

            /* Enable and Disable first plugin */
            HRESULT en_hr = TE_PluginLoaderEnable(&registry[0]);
            REQUIRE(SUCCEEDED(en_hr));
            REQUIRE(registry[0].enabled == true);

            HRESULT dis_hr = TE_PluginLoaderDisable(&registry[0]);
            REQUIRE(SUCCEEDED(dis_hr));
            REQUIRE(registry[0].enabled == false);
        }

        TE_PluginLoaderUnloadAll(registry, count);
    }
}
