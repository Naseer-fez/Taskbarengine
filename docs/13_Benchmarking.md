# 13 — Benchmarking Suite & Methodology

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: `Benchmarks/` (`bench_system.exe`, `te_benchmarks.exe`)

---

## 1. Overview & Tooling Architecture

TaskbarEngine includes two distinct benchmark suites:
1. **System-Level Telemetry Harness (`bench_system.exe`)**: Measures macro-level OS resource consumption on `explorer.exe` (Idle CPU, Active CPU, Working Set RAM, live DirectComposition FPS via IPC) and generates `benchmark_report.md`.
2. **Micro-Benchmark Suite (`te_benchmarks.exe`)**: In-process micro-benchmarks built using Google Benchmark to measure sub-microsecond algorithmic latency on core components.

```
Benchmarks/
├── CMakeLists.txt
├── bench_system.cpp            # System harness & Markdown report generator
├── bench_config_parse.cpp      # JSONC parse & diff micro-benchmark
├── bench_easing.cpp            # Easing curves (Gaussian/Cubic) micro-benchmark
├── bench_event_dispatch.cpp    # Event bus callback invocation micro-benchmark
├── bench_icon_layout.cpp       # Layout coordinate search micro-benchmark
├── bench_magnification.cpp     # Scale array computation micro-benchmark
└── bench_state_store.cpp       # Blackboard publish/query micro-benchmark
```

---

## 2. System Benchmark Harness (`bench_system.exe`)

### Command-Line Arguments
```powershell
# Quick 2-second sampling run (used in CI)
.\bench_system.exe --quick

# Full 60-second measurement run per architecture specification
.\bench_system.exe --full

# Specify custom report output file
.\bench_system.exe --full -o "D:\Reports\custom_benchmark.md"
```

### Telemetry Pipeline
1. **Explorer Process Discovery**: Locates `Shell_TrayWnd` and opens the hosting `explorer.exe` process with `PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`.
2. **Idle CPU & RAM**: Samples kernel and user execution time across the sample window while mouse cursor remains stationary.
3. **Active Cursor Sweeps**: Simulates physical mouse sweeps across the taskbar to measure active CPU overhead during animation bursts.
4. **IPC Engine Query**: Connects to `\\.\pipe\TaskbarEngine`, sends `TE_IPC_MSG_GET_PERF_STATS`, and extracts live hardware DComp frame rates and frame latency.
5. **Report Generation**: Emits a structured Markdown verification report.

---

## 3. Google Benchmark Micro-Benchmarks (`te_benchmarks.exe`)

Run the micro-benchmark suite from the build output directory:

```powershell
.\te_benchmarks.exe --benchmark_format=console
```

### Target Benchmarks
- **`BM_EventDispatch`**: Measures synchronous function pointer bus dispatch over 10 subscribers. (~0.001 µs/op).
- **`BM_GaussianMagnification`**: Measures per-icon Gaussian exponential scale calculation for 30 taskbar items. (~0.045 µs/op).
- **`BM_EasingCalculations`**: Measures Cubic, Bezier, and Cosine math curve evaluations. (~0.003 µs/op).
- **`BM_StateStorePublishQuery`**: Measures thread-safe blackboard hash table lookups. (~0.015 µs/op).
- **`BM_JsoncParse`**: Measures JSONC comment stripping and tree allocation for configuration files. (~12.5 µs/op).
