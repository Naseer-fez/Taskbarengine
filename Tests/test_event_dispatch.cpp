#include <catch2/catch_test_macros.hpp>
#include <core/event_dispatch.h>
#include <core/plugin_loader.h>

static int g_callback_count = 0;
static TE_EventType g_last_event_type = (TE_EventType)0;

static HRESULT TestEventCallback(uint32_t type, const void* event_data, void* user_data)
{
    (void)event_data;
    (void)user_data;
    g_callback_count++;
    g_last_event_type = (TE_EventType)type;
    return S_OK;
}

static int g_plugin1_count = 0;
static int g_plugin2_count = 0;

static HRESULT Plugin1Callback(uint32_t type, const void* event_data, void* user_data)
{
    (void)type; (void)event_data; (void)user_data;
    g_plugin1_count++;
    return S_OK;
}

static HRESULT Plugin2Callback(uint32_t type, const void* event_data, void* user_data)
{
    (void)type; (void)event_data; (void)user_data;
    g_plugin2_count++;
    return S_OK;
}

TEST_CASE("Event Dispatch Subscription Table", "[event]") {
    TE_EventEntry table[TE_MAX_SUBSCRIPTIONS];
    uint32_t count = 0;

    TE_EventDispatchInit(table, &count);
    REQUIRE(count == 0);

    SECTION("Subscribe and Dispatch") {
        g_callback_count = 0;
        HRESULT hr = TE_EventSubscribe(table, &count, TE_EVENT_CONFIG_CHANGED, TestEventCallback, nullptr, 1);
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(count == 1);

        TE_EventDispatch(table, count, TE_EVENT_CONFIG_CHANGED, nullptr);
        REQUIRE(g_callback_count == 1);
        REQUIRE(g_last_event_type == TE_EVENT_CONFIG_CHANGED);

        TE_EventDispatch(table, count, TE_EVENT_DISPLAY_CHANGED, nullptr);
        REQUIRE(g_callback_count == 1); /* Should not be called for non-subscribed event */
    }

    SECTION("Targeted Event Dispatch") {
        g_plugin1_count = 0;
        g_plugin2_count = 0;

        TE_EventSubscribe(table, &count, TE_EVENT_CONFIG_CHANGED, Plugin1Callback, nullptr, 1);
        TE_EventSubscribe(table, &count, TE_EVENT_CONFIG_CHANGED, Plugin2Callback, nullptr, 2);
        REQUIRE(count == 2);

        /* Targeted dispatch to plugin 1 only */
        TE_EventDispatchTargeted(table, count, TE_EVENT_CONFIG_CHANGED, nullptr, 1);
        REQUIRE(g_plugin1_count == 1);
        REQUIRE(g_plugin2_count == 0);

        /* Global dispatch (target = 0) reaches both */
        TE_EventDispatch(table, count, TE_EVENT_CONFIG_CHANGED, nullptr);
        REQUIRE(g_plugin1_count == 2);
        REQUIRE(g_plugin2_count == 1);
    }

    SECTION("Unsubscribe") {
        g_callback_count = 0;
        TE_EventSubscribe(table, &count, TE_EVENT_CONFIG_CHANGED, TestEventCallback, nullptr, 1);
        REQUIRE(count == 1);

        HRESULT hr = TE_EventUnsubscribe(table, &count, TE_EVENT_CONFIG_CHANGED, TestEventCallback);
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(count == 0);

        TE_EventDispatch(table, count, TE_EVENT_CONFIG_CHANGED, nullptr);
        REQUIRE(g_callback_count == 0);
    }

    SECTION("SubscribeEx and Plugin Disabled Filter") {
        TE_PluginEntry mock_entry{};
        mock_entry.enabled = false;
        mock_entry.disabled_by_fault = false;

        g_callback_count = 0;
        HRESULT hr = TE_EventSubscribeEx(table, &count, TE_EVENT_CONFIG_CHANGED, TestEventCallback, nullptr, 1, &mock_entry);
        REQUIRE(SUCCEEDED(hr));

        /* Disabled plugin should be filtered during dispatch */
        TE_EventDispatch(table, count, TE_EVENT_CONFIG_CHANGED, nullptr);
        REQUIRE(g_callback_count == 0);

        /* Enabling plugin allows dispatch */
        mock_entry.enabled = true;
        TE_EventDispatch(table, count, TE_EVENT_CONFIG_CHANGED, nullptr);
        REQUIRE(g_callback_count == 1);

        /* Fault disabled plugin is filtered again */
        mock_entry.disabled_by_fault = true;
        TE_EventDispatch(table, count, TE_EVENT_CONFIG_CHANGED, nullptr);
        REQUIRE(g_callback_count == 1);
    }
}
