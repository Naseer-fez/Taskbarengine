#include <benchmark/benchmark.h>
#include <core/state_store.h>

static void BM_StateStorePublishQuery(benchmark::State& state) {
    TE_StateStoreInit();
    StateValue val;
    val.type = TE_STATE_TYPE_INT;
    val.value.i = 42;

    for (auto _ : state) {
        TE_StatePublish("bench.key", &val);
        StateValue out;
        TE_StateQuery("bench.key", &out);
        benchmark::DoNotOptimize(out);
    }

    TE_StateStoreShutdown();
}
BENCHMARK(BM_StateStorePublishQuery);
