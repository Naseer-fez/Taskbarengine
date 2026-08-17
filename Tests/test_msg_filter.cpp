#include <catch2/catch_test_macros.hpp>
#include <core/te_msg_filter.h>

TEST_CASE("Message Filter - Initialization", "[msg_filter]") {
    REQUIRE(TE_MsgFilterInit() == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 0);
    REQUIRE_FALSE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE));
}

TEST_CASE("Message Filter - Subscribe and Query", "[msg_filter]") {
    REQUIRE(TE_MsgFilterInit() == S_OK);

    REQUIRE(TE_MsgFilterSubscribe(1, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 1);
    REQUIRE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE));
    REQUIRE_FALSE(TE_MsgFilterHasSubscriber(WM_LBUTTONDOWN));

    /* Duplicate subscribe is idempotent */
    REQUIRE(TE_MsgFilterSubscribe(1, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 1);
}

TEST_CASE("Message Filter - Unsubscribe Single", "[msg_filter]") {
    REQUIRE(TE_MsgFilterInit() == S_OK);

    REQUIRE(TE_MsgFilterSubscribe(1, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterSubscribe(1, WM_LBUTTONDOWN) == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 2);

    REQUIRE(TE_MsgFilterUnsubscribe(1, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 1);
    REQUIRE_FALSE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE));
    REQUIRE(TE_MsgFilterHasSubscriber(WM_LBUTTONDOWN));

    /* Unsubscribing non-existent returns S_FALSE */
    REQUIRE(TE_MsgFilterUnsubscribe(1, WM_MOUSEMOVE) == S_FALSE);
}

TEST_CASE("Message Filter - Multiple Plugins Same Message", "[msg_filter]") {
    REQUIRE(TE_MsgFilterInit() == S_OK);

    REQUIRE(TE_MsgFilterSubscribe(1, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterSubscribe(2, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 2);
    REQUIRE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE));

    /* Unsubscribe plugin 1; message still has subscriber plugin 2 */
    REQUIRE(TE_MsgFilterUnsubscribe(1, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 1);
    REQUIRE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE));

    /* Unsubscribe plugin 2; now has no subscriber */
    REQUIRE(TE_MsgFilterUnsubscribe(2, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 0);
    REQUIRE_FALSE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE));
}

TEST_CASE("Message Filter - UnsubscribeAll", "[msg_filter]") {
    REQUIRE(TE_MsgFilterInit() == S_OK);

    REQUIRE(TE_MsgFilterSubscribe(1, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterSubscribe(1, WM_LBUTTONDOWN) == S_OK);
    REQUIRE(TE_MsgFilterSubscribe(1, WM_RBUTTONUP) == S_OK);
    REQUIRE(TE_MsgFilterSubscribe(2, WM_MOUSEMOVE) == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 4);

    TE_MsgFilterUnsubscribeAll(1);
    REQUIRE(TE_MsgFilterGetCount() == 1);
    REQUIRE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE));
    REQUIRE_FALSE(TE_MsgFilterHasSubscriber(WM_LBUTTONDOWN));
    REQUIRE_FALSE(TE_MsgFilterHasSubscriber(WM_RBUTTONUP));

    TE_MsgFilterUnsubscribeAll(2);
    REQUIRE(TE_MsgFilterGetCount() == 0);
    REQUIRE_FALSE(TE_MsgFilterHasSubscriber(WM_MOUSEMOVE));
}

TEST_CASE("Message Filter - Invalid Arguments and Overflow", "[msg_filter]") {
    REQUIRE(TE_MsgFilterInit() == S_OK);

    REQUIRE(TE_MsgFilterSubscribe(0, WM_MOUSEMOVE) == E_INVALIDARG);
    REQUIRE(TE_MsgFilterUnsubscribe(0, WM_MOUSEMOVE) == E_INVALIDARG);

    /* Fill to capacity */
    for (uint32_t i = 1; i <= TE_MAX_MSG_SUBSCRIPTIONS; i++) {
        REQUIRE(TE_MsgFilterSubscribe(i, 0x1000 + i) == S_OK);
    }
    REQUIRE(TE_MsgFilterGetCount() == TE_MAX_MSG_SUBSCRIPTIONS);

    /* Next subscription beyond capacity should fail */
    REQUIRE(TE_MsgFilterSubscribe(999, 0x9999) == E_OUTOFMEMORY);

    /* Clean up */
    REQUIRE(TE_MsgFilterInit() == S_OK);
    REQUIRE(TE_MsgFilterGetCount() == 0);
}
