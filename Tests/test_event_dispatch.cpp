#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <sdk/te_types.h>
#include <sdk/te_events.h>
/* Forward-declare event dispatch functions */
typedef void (*TE_EventCallback)(uint32_t event_type, const void* event_data, void* user_data);
HRESULT TE_EventDispatchInit(void);
void TE_EventDispatchShutdown(void);
HRESULT TE_EventDispatchSubscribe(uint32_t event_type, TE_EventCallback callback,
                                   void* user_data, uint32_t plugin_id);
HRESULT TE_EventDispatchUnsubscribe(uint32_t event_type, TE_EventCallback callback);
void TE_EventDispatchFire(uint32_t event_type, const void* event_data);
void TE_EventDispatchRemoveByPlugin(uint32_t plugin_id);
uint32_t TE_EventDispatchGetCount(void);
}

static int g_callback_count = 0;
static uint32_t g_last_event_type = 0;
static const void* g_last_event_data = nullptr;

static void TestCallback(uint32_t event_type, const void* event_data, void* user_data) {
    (void)user_data;
    g_callback_count++;
    g_last_event_type = event_type;
    g_last_event_data = event_data;
}

static void TestCallback2(uint32_t event_type, const void* event_data, void* user_data) {
    (void)event_type;
    (void)event_data;
    int* counter = (int*)user_data;
    if (counter) (*counter)++;
}

TEST_CASE("Event dispatch subscribe and fire", "[events]") {
    TE_EventDispatchInit();
    g_callback_count = 0;
    g_last_event_type = 0;

    SECTION("Subscribe and fire") {
        CHECK(TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, TestCallback, nullptr, 1) == TE_S_OK);
        CHECK(TE_EventDispatchGetCount() == 1);

        int dummy_data = 42;
        TE_EventDispatchFire(TE_EVENT_CONFIG_CHANGED, &dummy_data);
        CHECK(g_callback_count == 1);
        CHECK(g_last_event_type == TE_EVENT_CONFIG_CHANGED);
        CHECK(g_last_event_data == &dummy_data);
    }

    SECTION("Event type filtering") {
        TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, TestCallback, nullptr, 1);
        TE_EventDispatchFire(TE_EVENT_DPI_CHANGED, nullptr);
        CHECK(g_callback_count == 0); /* Should not fire for wrong type */
    }

    SECTION("Unsubscribe") {
        TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, TestCallback, nullptr, 1);
        CHECK(TE_EventDispatchGetCount() == 1);
        CHECK(TE_EventDispatchUnsubscribe(TE_EVENT_CONFIG_CHANGED, TestCallback) == TE_S_OK);
        CHECK(TE_EventDispatchGetCount() == 0);

        TE_EventDispatchFire(TE_EVENT_CONFIG_CHANGED, nullptr);
        CHECK(g_callback_count == 0);
    }

    SECTION("Remove by plugin ID") {
        TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, TestCallback, nullptr, 1);
        int counter = 0;
        TE_EventDispatchSubscribe(TE_EVENT_DPI_CHANGED, TestCallback2, &counter, 2);
        CHECK(TE_EventDispatchGetCount() == 2);

        TE_EventDispatchRemoveByPlugin(1);
        CHECK(TE_EventDispatchGetCount() == 1);

        TE_EventDispatchFire(TE_EVENT_CONFIG_CHANGED, nullptr);
        CHECK(g_callback_count == 0);

        TE_EventDispatchFire(TE_EVENT_DPI_CHANGED, nullptr);
        CHECK(counter == 1);
    }

    SECTION("Multiple subscribers same event") {
        int counter = 0;
        TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, TestCallback, nullptr, 1);
        TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, TestCallback2, &counter, 2);
        CHECK(TE_EventDispatchGetCount() == 2);

        TE_EventDispatchFire(TE_EVENT_CONFIG_CHANGED, nullptr);
        CHECK(g_callback_count == 1);
        CHECK(counter == 1);
    }

    SECTION("Max subscriptions") {
        for (uint32_t i = 0; i < 64; i++) {
            CHECK(TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, TestCallback, nullptr, i + 1) == TE_S_OK);
        }
        CHECK(TE_EventDispatchGetCount() == 64);
        CHECK(TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, TestCallback, nullptr, 99) == TE_E_FAIL);
    }

    TE_EventDispatchShutdown();
}
