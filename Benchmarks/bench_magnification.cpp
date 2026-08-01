#include <benchmark/benchmark.h>
#include "../Modules/icon_hover/magnification.h"
#include <vector>

static void BM_MagnificationBatch20(benchmark::State& state) {
    std::vector<float> centers(20);
    for (int i = 0; i < 20; ++i) {
        centers[i] = (float)(i * 40);
    }
    std::vector<float> scales(20, 1.0f);
    float cursor_x = 400.0f;
    float radius = 120.0f;
    float max_scale = 1.30f;

    for (auto _ : state) {
        TE_MagnifyComputeScales(cursor_x, centers.data(), scales.data(), 20, radius, max_scale, TE_CURVE_GAUSSIAN);
        benchmark::DoNotOptimize(scales.data());
    }
}
BENCHMARK(BM_MagnificationBatch20);

BENCHMARK_MAIN();
