#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <algorithm>

extern "C" {
#include <core/plugin_loader.h>
#include <core/taskbar_subclass.h>

HRESULT TE_TaskbarSubclassSubscribeMessage(UINT msg) {
    (void)msg;
    return TE_S_OK;
}
HRESULT TE_TaskbarSubclassUnsubscribeMessage(UINT msg) {
    (void)msg;
    return TE_S_OK;
}
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

    CHECK(TE_PluginLoaderShutdown() == TE_S_OK);
}

TEST_CASE("Load icon_hover plugin DLL directly", "[plugins][icon_hover]") {
    HMODULE hMod = LoadLibraryW(L"Modules/icon_hover/icon_hover.dll");
    if (!hMod) {
        hMod = LoadLibraryW(L"D:/CODE/Utlities/Taskbar/build_msvc/Modules/icon_hover/icon_hover.dll");
    }
    REQUIRE(hMod != nullptr);

    typedef const PluginInterface* (*GetPluginInterfaceFunc)(void);
    GetPluginInterfaceFunc get_iface = (GetPluginInterfaceFunc)GetProcAddress(hMod, "GetPluginInterface");
    REQUIRE(get_iface != nullptr);

    const PluginInterface* iface = get_iface();
    REQUIRE(iface != nullptr);
    REQUIRE(iface->Initialize != nullptr);
    REQUIRE(iface->Enable != nullptr);
    REQUIRE(iface->Disable != nullptr);
    REQUIRE(iface->GetMetadata != nullptr);

    const PluginMetadata* meta = iface->GetMetadata();
    REQUIRE(meta != nullptr);
    REQUIRE(strcmp(meta->name, "icon_hover") == 0);

    FreeLibrary(hMod);
}
