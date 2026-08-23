#include <catch2/catch_test_macros.hpp>
#include <core/plugin_loader.h>
#include <core/fault_isolation.h>
#include <core/event_dispatch.h>
#include <core/te_msg_filter.h>
#include <core/te_timer.h>
#include <windows.h>
#include <cstring>

#ifndef TE_TEST_MODULES_DIR
#define TE_TEST_MODULES_DIR L"Modules"
#endif

static void StabilityDummyTimerCb(void* data) { (void)data; }
static HRESULT StabilityDummyEventCb(uint32_t type, const void* data, void* udata) {
    (void)type; (void)data; (void)udata;
    return S_OK;
}

#ifdef _MSC_VER
TEST_CASE("Stability - Fault Isolation and Auto-Disable", "[stability][fault]") {
    TE_PluginEntry registry[TE_MAX_PLUGINS];
    memset(registry, 0, sizeof(registry));
    uint32_t count = 0;

    HRESULT hr = TE_PluginLoaderScan(TE_TEST_MODULES_DIR, registry, &count);
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(count >= 1);

    TE_PluginEntry* fault = nullptr;
    for (uint32_t i = 0; i < count; i++) {
        if (registry[i].metadata && registry[i].metadata->name &&
            strcmp(registry[i].metadata->name, "FaultPlugin") == 0) {
            fault = &registry[i];
            break;
        }
    }

    if (fault) {
        /* Initialize fault plugin */
        if (fault->iface && fault->iface->Initialize) {
            fault->iface->Initialize(fault->context);
        }

        /* 1st Fault */
        HRESULT f1 = TE_PluginLoaderEnable(fault);
        REQUIRE(FAILED(f1));
        REQUIRE(fault->fault_count == 1);
        REQUIRE_FALSE(fault->disabled_by_fault);

        /* 2nd Fault */
        HRESULT f2 = TE_PluginLoaderEnable(fault);
        REQUIRE(FAILED(f2));
        REQUIRE(fault->fault_count == 2);
        REQUIRE_FALSE(fault->disabled_by_fault);

        /* 3rd Fault -> Auto-Disabled */
        HRESULT f3 = TE_PluginLoaderEnable(fault);
        REQUIRE(FAILED(f3));
        REQUIRE(fault->fault_count >= 3);
        REQUIRE(fault->disabled_by_fault == true);

        /* 4th Attempt is blocked immediately due to disabled_by_fault */
        HRESULT f4 = TE_PluginLoaderEnable(fault);
        REQUIRE(FAILED(f4));
    }

    TE_PluginLoaderUnloadAll(registry, count);
}
#endif

TEST_CASE("Stability - Rapid Enable and Disable Cycles (100 iterations)", "[stability]") {
    TE_PluginEntry registry[TE_MAX_PLUGINS];
    memset(registry, 0, sizeof(registry));
    uint32_t count = 0;

    HRESULT hr = TE_PluginLoaderScan(TE_TEST_MODULES_DIR, registry, &count);
    REQUIRE(SUCCEEDED(hr));

    TE_PluginEntry* dummy = nullptr;
    for (uint32_t i = 0; i < count; i++) {
        if (registry[i].metadata && registry[i].metadata->name &&
            strcmp(registry[i].metadata->name, "DummyPlugin") == 0) {
            dummy = &registry[i];
            break;
        }
    }

    REQUIRE(dummy != nullptr);
    if (dummy->iface && dummy->iface->Initialize) {
        dummy->iface->Initialize(dummy->context);
    }

    for (int i = 0; i < 100; i++) {
        HRESULT en_hr = TE_PluginLoaderEnable(dummy);
        REQUIRE(SUCCEEDED(en_hr));
        REQUIRE(dummy->enabled == true);

        HRESULT dis_hr = TE_PluginLoaderDisable(dummy);
        REQUIRE(SUCCEEDED(dis_hr));
        REQUIRE(dummy->enabled == false);
    }

    TE_PluginLoaderUnloadAll(registry, count);
}

TEST_CASE("Stability - Resource and Subscription Cleanup on Unload", "[stability]") {
    REQUIRE(TE_MsgFilterInit() == S_OK);
    REQUIRE(TE_TimerInit(NULL) == S_OK);

    TE_EventEntry subs[TE_MAX_SUBSCRIPTIONS];
    memset(subs, 0, sizeof(subs));
    uint32_t sub_count = 0;
    TE_EventDispatchInit(subs, &sub_count);

    /* Plugin 1 subscribes to events, messages, and timers */
    REQUIRE(TE_EventSubscribeEx(subs, &sub_count, TE_EVENT_TASKBAR_MOUSE, StabilityDummyEventCb, NULL, 1, NULL) == S_OK);
    REQUIRE(TE_MsgFilterSubscribe(1, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_TimerCreate(StabilityDummyTimerCb, NULL, 100, TRUE, 1, NULL) == S_OK);

    REQUIRE(sub_count == 1);
    REQUIRE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE) == TRUE);
    REQUIRE(TE_TimerGetActiveCount() == 1);

    /* Simulate plugin 1 disable/unload */
    TE_EventUnsubscribe(subs, &sub_count, TE_EVENT_TASKBAR_MOUSE, StabilityDummyEventCb);
    TE_MsgFilterUnsubscribeAll(1);
    TE_TimerCancelAllForPlugin(1);

    REQUIRE(sub_count == 0);
    REQUIRE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE) == FALSE);
    REQUIRE(TE_TimerGetActiveCount() == 0);

    TE_TimerShutdown();
}
