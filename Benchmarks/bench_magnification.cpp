#include <benchmark/benchmark.h>
#include "../Modules/icon_hover/magnification.h"
#include <vector>

static void BM_MagnifyComputeScales(benchmark::State& state) {
    TE_MagnifyCurveType curve = (TE_MagnifyCurveType)state.range(0);
    int count = 20;
    std::vector<float> centers(count);
    for (int i = 0; i < count; i++) {
        centers[i] = i * 40.0f;
    }
    std::vector<float> scales(count);
    float cursor_x = 80.0f;
    float radius = 100.0f;
    float max_scale = 1.5f;

    for (auto _ : state) {
        TE_MagnifyComputeScales(cursor_x, centers.data(), scales.data(), count, radius, max_scale, curve);
        benchmark::DoNotOptimize(scales);
    }
}
BENCHMARK(BM_MagnifyComputeScales)->DenseRange(0, 3, 1);
