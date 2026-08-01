#include <catch2/catch_test_macros.hpp>
#include <core/state_store.h>
#include <thread>
#include <vector>

TEST_CASE("StateStore basic publish and query", "[state_store]") {
    REQUIRE(TE_StateStoreInit() == S_OK);

    SECTION("Publish int") {
        StateValue v_in;
        v_in.type = TE_STATE_TYPE_INT;
        v_in.value.i = 42;
        REQUIRE(TE_StatePublish("test.int", &v_in) == S_OK);

        StateValue v_out;
        REQUIRE(TE_StateQuery("test.int", &v_out) == S_OK);
        REQUIRE(v_out.type == TE_STATE_TYPE_INT);
        REQUIRE(v_out.value.i == 42);
    }

    SECTION("Publish float") {
        StateValue v_in;
        v_in.type = TE_STATE_TYPE_FLOAT;
        v_in.value.f = 1.30f;
        REQUIRE(TE_StatePublish("test.float", &v_in) == S_OK);

        StateValue v_out;
        REQUIRE(TE_StateQuery("test.float", &v_out) == S_OK);
        REQUIRE(v_out.type == TE_STATE_TYPE_FLOAT);
        REQUIRE(v_out.value.f == 1.30f);
    }

    SECTION("Query non-existent key") {
        StateValue v_out;
        REQUIRE(TE_StateQuery("non.existent", &v_out) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    }

    SECTION("Overwrite key") {
        StateValue v1; v1.type = TE_STATE_TYPE_INT; v1.value.i = 10;
        StateValue v2; v2.type = TE_STATE_TYPE_INT; v2.value.i = 20;

        REQUIRE(TE_StatePublish("test.key", &v1) == S_OK);
        REQUIRE(TE_StatePublish("test.key", &v2) == S_OK);

        StateValue v_out;
        REQUIRE(TE_StateQuery("test.key", &v_out) == S_OK);
        REQUIRE(v_out.value.i == 20);
    }

    TE_StateStoreShutdown();
}

TEST_CASE("StateStore concurrent multithreaded access", "[state_store]") {
    REQUIRE(TE_StateStoreInit() == S_OK);

    StateValue initial;
    initial.type = TE_STATE_TYPE_INT;
    initial.value.i = 100;
    TE_StatePublish("concurrent.key", &initial);

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([i]() {
            for (int k = 0; k < 1000; ++k) {
                StateValue v;
                v.type = TE_STATE_TYPE_INT;
                v.value.i = i * 1000 + k;
                TE_StatePublish("concurrent.key", &v);

                StateValue out;
                TE_StateQuery("concurrent.key", &out);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    TE_StateStoreShutdown();
}
