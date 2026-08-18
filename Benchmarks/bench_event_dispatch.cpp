#include <benchmark/benchmark.h>
#include <core/event_dispatch.h>

extern "C" {
    static uint32_t g_bench_plugin_id = 0;
    uint32_t TE_CoreManagerGetCurrentPluginId(void) { return g_bench_plugin_id; }
    void TE_CoreManagerSetCurrentPluginId(uint32_t id) { g_bench_plugin_id = id; }
}

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

static void BM_EventDispatchMultiSubscriber(benchmark::State& state)
{
    TE_EventEntry table[TE_MAX_SUBSCRIPTIONS];
    uint32_t count = 0;
    TE_EventDispatchInit(table, &count);
    int num_subscribers = (int)state.range(0);
    for (int i = 0; i < num_subscribers; i++) {
        TE_EventSubscribe(table, &count, TE_EVENT_CONFIG_CHANGED, BenchCallback, nullptr, (uint32_t)(i + 1));
    }

    for (auto _ : state) {
        TE_EventDispatch(table, count, TE_EVENT_CONFIG_CHANGED, nullptr);
    }
    state.SetItemsProcessed(state.iterations() * num_subscribers);
}

BENCHMARK(BM_EventDispatchMultiSubscriber)->Arg(1)->Arg(4)->Arg(8)->Arg(16);

static void BM_EventDispatchNoSubscriber(benchmark::State& state)
{
    TE_EventEntry table[TE_MAX_SUBSCRIPTIONS];
    uint32_t count = 0;
    TE_EventDispatchInit(table, &count);

    for (auto _ : state) {
        TE_EventDispatch(table, count, TE_EVENT_CONFIG_CHANGED, nullptr);
    }
}

BENCHMARK(BM_EventDispatchNoSubscriber);
