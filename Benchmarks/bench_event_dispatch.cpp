#include <benchmark/benchmark.h>
#include <core/event_dispatch.h>

static HRESULT BenchCallback(uint32_t type, const void* event_data, void* user_data)
{
    benchmark::DoNotOptimize(type);
    benchmark::DoNotOptimize(event_data);
    benchmark::DoNotOptimize(user_data);
    return S_OK;
}

static void BM_EventDispatchSingleSubscriber(benchmark::State& state)
{
    TE_EventEntry table[TE_MAX_SUBSCRIPTIONS];
    uint32_t count = 0;
    TE_EventDispatchInit(table, &count);
    TE_EventSubscribe(table, &count, TE_EVENT_CONFIG_CHANGED, BenchCallback, nullptr, 1);

    for (auto _ : state) {
        TE_EventDispatch(table, count, TE_EVENT_CONFIG_CHANGED, nullptr);
    }
}

BENCHMARK(BM_EventDispatchSingleSubscriber);
