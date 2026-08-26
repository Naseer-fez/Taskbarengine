#include <benchmark/benchmark.h>
#include <core/state_store.h>

static void BM_StateStorePublishQuery(benchmark::State& state) {
    StateValue val;
    val.type = TE_STATE_INT;
    val.data.int_val = 42;

    for (auto _ : state) {
        TE_StatePublish("bench.key", &val);
        StateValue out;
        TE_StateQuery("bench.key", &out);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_StateStorePublishQuery);
