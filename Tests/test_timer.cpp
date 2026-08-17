#include <catch2/catch_test_macros.hpp>
#include <core/te_timer.h>
#include <windows.h>

static volatile LONG g_test_fire_count = 0;

static void TestTimerCallback(void* user_data)
{
    (void)user_data;
    InterlockedIncrement(&g_test_fire_count);
}

TEST_CASE("Timer - Initialization and Shutdown", "[timer]") {
    REQUIRE(TE_TimerInit(NULL) == S_OK);
    REQUIRE(TE_TimerGetActiveCount() == 0);
    TE_TimerShutdown();
    REQUIRE(TE_TimerGetActiveCount() == 0);
}

TEST_CASE("Timer - Invalid Arguments", "[timer]") {
    REQUIRE(TE_TimerInit(NULL) == S_OK);

    REQUIRE(TE_TimerCreate(NULL, NULL, 50, FALSE, 1, NULL) == E_INVALIDARG);
    REQUIRE(TE_TimerCreate(TestTimerCallback, NULL, 0, FALSE, 1, NULL) == E_INVALIDARG);
    REQUIRE(TE_TimerCancelById(0) == E_INVALIDARG);
    REQUIRE(TE_TimerCancelByCallback(NULL, 1) == E_INVALIDARG);

    TE_TimerShutdown();
}

TEST_CASE("Timer - One-Shot Direct Execution", "[timer]") {
    REQUIRE(TE_TimerInit(NULL) == S_OK);
    g_test_fire_count = 0;

    uint32_t timer_id = 0;
    REQUIRE(TE_TimerCreate(TestTimerCallback, NULL, 20, FALSE, 1, &timer_id) == S_OK);
    REQUIRE(timer_id > 0);

    /* Wait for one-shot timer to fire on test thread pool fallback */
    Sleep(100);

    REQUIRE(g_test_fire_count >= 1);
    /* One-shot timer auto-cleans up after firing */
    REQUIRE(TE_TimerGetActiveCount() == 0);

    TE_TimerShutdown();
}

TEST_CASE("Timer - Recurring and Cancellation By ID", "[timer]") {
    REQUIRE(TE_TimerInit(NULL) == S_OK);
    g_test_fire_count = 0;

    uint32_t timer_id = 0;
    REQUIRE(TE_TimerCreate(TestTimerCallback, NULL, 20, TRUE, 1, &timer_id) == S_OK);
    REQUIRE(timer_id > 0);
    REQUIRE(TE_TimerGetActiveCount() == 1);

    Sleep(80);
    LONG count_before = g_test_fire_count;
    REQUIRE(count_before >= 1);

    REQUIRE(TE_TimerCancelById(timer_id) == S_OK);
    REQUIRE(TE_TimerGetActiveCount() == 0);

    /* Verify it no longer fires after cancellation */
    Sleep(60);
    REQUIRE(g_test_fire_count == count_before);

    TE_TimerShutdown();
}

TEST_CASE("Timer - Cancellation By Callback and Plugin ID", "[timer]") {
    REQUIRE(TE_TimerInit(NULL) == S_OK);
    g_test_fire_count = 0;

    REQUIRE(TE_TimerCreate(TestTimerCallback, NULL, 30, TRUE, 1, NULL) == S_OK);
    REQUIRE(TE_TimerCreate(TestTimerCallback, NULL, 30, TRUE, 2, NULL) == S_OK);
    REQUIRE(TE_TimerGetActiveCount() == 2);

    /* Cancel only plugin 1's timer */
    REQUIRE(TE_TimerCancelByCallback(TestTimerCallback, 1) == S_OK);
    REQUIRE(TE_TimerGetActiveCount() == 1);

    /* Cancel plugin 2's timer */
    REQUIRE(TE_TimerCancelByCallback(TestTimerCallback, 2) == S_OK);
    REQUIRE(TE_TimerGetActiveCount() == 0);

    TE_TimerShutdown();
}

TEST_CASE("Timer - CancelAllForPlugin", "[timer]") {
    REQUIRE(TE_TimerInit(NULL) == S_OK);

    REQUIRE(TE_TimerCreate(TestTimerCallback, NULL, 100, TRUE, 1, NULL) == S_OK);
    REQUIRE(TE_TimerCreate(TestTimerCallback, NULL, 100, TRUE, 1, NULL) == S_OK);
    REQUIRE(TE_TimerCreate(TestTimerCallback, NULL, 100, TRUE, 2, NULL) == S_OK);
    REQUIRE(TE_TimerGetActiveCount() == 3);

    TE_TimerCancelAllForPlugin(1);
    REQUIRE(TE_TimerGetActiveCount() == 1);

    TE_TimerCancelAllForPlugin(2);
    REQUIRE(TE_TimerGetActiveCount() == 0);

    TE_TimerShutdown();
}

TEST_CASE("Timer - Dispatch Message Simulation", "[timer]") {
    /* Test simulating UI-thread dispatch message handling */
    REQUIRE(TE_TimerInit((HWND)(uintptr_t)0x12345678) == S_OK);
    g_test_fire_count = 0;

    uint32_t timer_id = 0;
    REQUIRE(TE_TimerCreate(TestTimerCallback, NULL, 1000, FALSE, 1, &timer_id) == S_OK);

    /* Simulate UI window proc handling WM_TE_TIMER_FIRE */
    TE_TimerDispatchMessage((WPARAM)timer_id, 0);

    REQUIRE(g_test_fire_count == 1);
    REQUIRE(TE_TimerGetActiveCount() == 0);

    TE_TimerShutdown();
}
