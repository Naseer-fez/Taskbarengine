#include <benchmark/benchmark.h>
#include <sdk/te_easing.h>

static void BM_EasingApply(benchmark::State& state) {
    TE_EasingType type = (TE_EasingType)state.range(0);
    float t = 0.0f;
    float step = 0.001f;
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(TE_EasingApply(t, type));
        t += step;
        if (t > 1.0f) t = 0.0f;
    }
}
BENCHMARK(BM_EasingApply)->DenseRange(0, 7, 1);
