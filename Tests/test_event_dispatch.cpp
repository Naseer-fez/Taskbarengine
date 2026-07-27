#include <catch2/catch_test_macros.hpp>
#include <core/event_dispatch.h>

static int g_callback_count = 0;
static TE_EventType g_last_event_type = (TE_EventType)0;

static void TestEventCallback(TE_EventType type, const void* event_data, void* user_data)
{
    (void)event_data;
    (void)user_data;
    g_callback_count++;
    g_last_event_type = type;
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
}
