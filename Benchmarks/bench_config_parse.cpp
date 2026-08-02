#include <benchmark/benchmark.h>
#include <sdk/te_jsonc.h>
#include <cJSON.h>

static const char* MOCK_CONFIG = R"(
{
    "version": 1,
    "core": {
        "log_level": "info",
        "log_to_file": false
    },
    "plugin": {
        "icon_hover": {
            "scale": 1.3,
            "radius": 120,
            "curve": "gaussian",
            "speed_ms": 150
        }
    }
}
)";

static void BM_JsoncParse(benchmark::State& state) {
    for (auto _ : state) {
        cJSON* root = nullptr;
        HRESULT hr = TE_JsoncParseString(MOCK_CONFIG, &root);
        if (SUCCEEDED(hr) && root) {
            TE_JsoncFree(root);
        }
    }
}
BENCHMARK(BM_JsoncParse);
