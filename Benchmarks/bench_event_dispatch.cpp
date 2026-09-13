#include <benchmark/benchmark.h>
#include <core/event_dispatch.h>
#include <sdk/te_events.h>

static void BenchCallback(uint32_t type, const void* event_data, void* user_data)
{
    benchmark::DoNotOptimize(type);
    benchmark::DoNotOptimize(event_data);
    benchmark::DoNotOptimize(user_data);
}

static void BM_EventDispatchSingleSubscriber(benchmark::State& state)
{
    TE_EventDispatchInit();
    TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, BenchCallback, nullptr, 1);

    for (auto _ : state) {
        TE_EventDispatchFire(TE_EVENT_CONFIG_CHANGED, nullptr);
    }
    
    TE_EventDispatchShutdown();
}

BENCHMARK(BM_EventDispatchSingleSubscriber);

static void BM_EventDispatchMultiSubscriber(benchmark::State& state)
{
    TE_EventDispatchInit();
    int num_subscribers = (int)state.range(0);
    for (int i = 0; i < num_subscribers; i++) {
        TE_EventDispatchSubscribe(TE_EVENT_CONFIG_CHANGED, BenchCallback, nullptr, (uint32_t)(i + 1));
    }

    for (auto _ : state) {
        TE_EventDispatchFire(TE_EVENT_CONFIG_CHANGED, nullptr);
    }
    state.SetItemsProcessed(state.iterations() * num_subscribers);
    
    TE_EventDispatchShutdown();
}

BENCHMARK(BM_EventDispatchMultiSubscriber)->Arg(1)->Arg(4)->Arg(8)->Arg(16);

static void BM_EventDispatchNoSubscriber(benchmark::State& state)
{
    TE_EventDispatchInit();

    for (auto _ : state) {
        TE_EventDispatchFire(TE_EVENT_CONFIG_CHANGED, nullptr);
    }
    
    TE_EventDispatchShutdown();
}

BENCHMARK(BM_EventDispatchNoSubscriber);
