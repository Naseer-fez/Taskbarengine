#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/state_store.h"
#include <stdio.h>

TEST_CASE("State Store Operations", "[state_store]") {
    TE_StateStoreInit();

    SECTION("Publish and query int") {
        StateValue val;
        val.type = TE_STATE_INT;
        val.data.int_val = 42;
        REQUIRE(TE_StatePublish("test.int", &val) == TE_S_OK);

        StateValue outVal;
        REQUIRE(TE_StateQuery("test.int", &outVal) == TE_S_OK);
        REQUIRE(outVal.type == TE_STATE_INT);
        REQUIRE(outVal.data.int_val == 42);
    }

    SECTION("Publish and query float") {
        StateValue val;
        val.type = TE_STATE_FLOAT;
        val.data.float_val = 3.14f;
        REQUIRE(TE_StatePublish("test.float", &val) == TE_S_OK);

        StateValue outVal;
        REQUIRE(TE_StateQuery("test.float", &outVal) == TE_S_OK);
        REQUIRE(outVal.type == TE_STATE_FLOAT);
        REQUIRE(outVal.data.float_val == Catch::Approx(3.14f));
    }

    SECTION("Publish and query bool") {
        StateValue val;
        val.type = TE_STATE_BOOL;
        val.data.bool_val = 1;
        REQUIRE(TE_StatePublish("test.bool", &val) == TE_S_OK);

        StateValue outVal;
        REQUIRE(TE_StateQuery("test.bool", &outVal) == TE_S_OK);
        REQUIRE(outVal.type == TE_STATE_BOOL);
        REQUIRE(outVal.data.bool_val == 1);
    }

    SECTION("Publish and query RECT") {
        StateValue val;
        val.type = TE_STATE_RECT;
        val.data.rect_val.left = 10;
        val.data.rect_val.top = 20;
        val.data.rect_val.right = 30;
        val.data.rect_val.bottom = 40;
        REQUIRE(TE_StatePublish("test.rect", &val) == TE_S_OK);

        StateValue outVal;
        REQUIRE(TE_StateQuery("test.rect", &outVal) == TE_S_OK);
        REQUIRE(outVal.type == TE_STATE_RECT);
        REQUIRE(outVal.data.rect_val.left == 10);
        REQUIRE(outVal.data.rect_val.top == 20);
        REQUIRE(outVal.data.rect_val.right == 30);
        REQUIRE(outVal.data.rect_val.bottom == 40);
    }

    SECTION("Query non-existent key returns TE_E_FAIL") {
        StateValue outVal;
        REQUIRE(TE_StateQuery("nonexistent.key", &outVal) == TE_E_FAIL);
    }

    SECTION("Overwrite existing key") {
        StateValue val1;
        val1.type = TE_STATE_INT;
        val1.data.int_val = 10;
        REQUIRE(TE_StatePublish("overwrite.key", &val1) == TE_S_OK);

        StateValue val2;
        val2.type = TE_STATE_INT;
        val2.data.int_val = 20;
        REQUIRE(TE_StatePublish("overwrite.key", &val2) == TE_S_OK);

        StateValue outVal;
        REQUIRE(TE_StateQuery("overwrite.key", &outVal) == TE_S_OK);
        REQUIRE(outVal.data.int_val == 20);
    }

    SECTION("Multiple distinct keys") {
        for (int i = 0; i < 10; i++) {
            char key[32];
            snprintf(key, sizeof(key), "key.%d", i);
            StateValue val;
            val.type = TE_STATE_INT;
            val.data.int_val = i * 100;
            REQUIRE(TE_StatePublish(key, &val) == TE_S_OK);
        }

        for (int i = 0; i < 10; i++) {
            char key[32];
            snprintf(key, sizeof(key), "key.%d", i);
            StateValue outVal;
            REQUIRE(TE_StateQuery(key, &outVal) == TE_S_OK);
            REQUIRE(outVal.data.int_val == i * 100);
        }
    }

    SECTION("Fill to capacity and overflow") {
        for (int i = 0; i < 256; i++) {
            char key[32];
            snprintf(key, sizeof(key), "fill.%d", i);
            StateValue val;
            val.type = TE_STATE_INT;
            val.data.int_val = i;
            REQUIRE(TE_StatePublish(key, &val) == TE_S_OK);
        }

        StateValue valExtra;
        valExtra.type = TE_STATE_INT;
        valExtra.data.int_val = 999;
        REQUIRE(TE_StatePublish("overflow.key", &valExtra) == TE_E_OUTOFMEMORY);
    }

    SECTION("Null parameter handling") {
        StateValue val;
        REQUIRE(TE_StatePublish(NULL, &val) == TE_E_INVALIDARG);
        REQUIRE(TE_StatePublish("test", NULL) == TE_E_INVALIDARG);
        REQUIRE(TE_StateQuery(NULL, &val) == TE_E_INVALIDARG);
        REQUIRE(TE_StateQuery("test", NULL) == TE_E_INVALIDARG);
    }

    SECTION("Init/Shutdown lifecycle") {
        StateValue val;
        val.type = TE_STATE_INT;
        val.data.int_val = 123;
        REQUIRE(TE_StatePublish("lifecycle.key", &val) == TE_S_OK);

        TE_StateStoreShutdown();
        TE_StateStoreInit();

        StateValue outVal;
        REQUIRE(TE_StateQuery("lifecycle.key", &outVal) == TE_E_FAIL);
    }

    TE_StateStoreShutdown();
}
