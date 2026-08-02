#include <benchmark/benchmark.h>
#include "../Modules/icon_hover/icon_layout.h"
#include <vector>

static void BM_IconLayoutCompute(benchmark::State& state) {
    int count = 20;
    std::vector<float> scales(count, 1.0f);
    scales[10] = 1.5f;
    scales[9] = 1.25f;
    scales[11] = 1.25f;

    std::vector<float> base_x(count);
    for (int i = 0; i < count; i++) {
        base_x[i] = i * 40.0f;
    }

    std::vector<float> out_x(count);
    std::vector<float> out_y(count);

    for (auto _ : state) {
        TE_LayoutComputePositions(scales.data(), base_x.data(), out_x.data(), out_y.data(), count, 32.0f, 1000.0f);
        benchmark::DoNotOptimize(out_x);
        benchmark::DoNotOptimize(out_y);
    }
}
BENCHMARK(BM_IconLayoutCompute);
