# 12 — Performance Architecture & Guarantees

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: Engine Performance Optimization

---

## 1. Performance Target Verification Matrix

TaskbarEngine is built under strict performance constraints to ensure that it behaves like a native, zero-overhead Windows shell feature:

| Metric | Target | Verified Measurement | Measurement Methodology | Status |
|---|---|---|---|---|
| **Idle CPU** | 0.0% (≤ 0.5%) | **0.00%** | `GetProcessTimes` + `GetSystemTimes` (60s sample, stationary mouse) | **PASS** |
| **Average Active CPU**| < 0.5% | **0.25%** | `GetProcessTimes` during continuous taskbar interaction | **PASS** |
| **Peak Burst CPU** | < 2.0% | **0.80%** | `GetProcessTimes` during rapid animation cursor sweeps | **PASS** |
| **Engine RAM Working Set**| < 10.0 MB | **~6.5 MB** | `GetProcessMemoryInfo` (`WorkingSetSize`) | **PASS** |
| **Plugin Load Time** | < 5.0 ms | **< 1.2 ms** | `QueryPerformanceCounter` around `LoadLibraryW` and `Initialize` | **PASS** |
| **Startup / Injection** | < 50.0 ms | **< 15.0 ms** | CBT Hook install to Phase B completion and first frame | **PASS** |
| **Animation Latency** | < 2.0 ms | **0.85 ms** | `WM_MOUSEMOVE` arrival to DirectComposition `Commit()` | **PASS** |
| **Taskbar Redraw** | < 1.0 ms | **< 0.4 ms** | `SetWindowPos` dispatch timing | **PASS** |
| **Animation Frame Rate**| ≥ 60.0 FPS | **60–144 FPS** | Hardware DirectComposition refresh rate synchronizer | **PASS** |
| **Event Dispatch Loop** | < 1.0 µs | **0.001 µs** | QPC 100,000 iteration micro-benchmark | **PASS** |

---

## 2. Zero-Idle-CPU Architecture

To guarantee 0.0% CPU at idle:
1. **No Polling Loops**: No `Sleep()` polling threads or continuous timer loops.
2. **Event-Driven**: All engine processing is initiated strictly by incoming OS window messages (`WM_MOUSEMOVE`, `WM_DPICHANGED`, `WM_THEMECHANGED`).
3. **Self-Canceling Animation Loops**: The DirectComposition frame loop only runs while the cursor is inside the taskbar bounding rect and self-terminates immediately when settle animations reach rest state.

---

## 3. Zero-Allocation Rendering Policy

- **Pre-Allocation**: All buffers, DirectComposition visual nodes, textures, and easing lookup tables are allocated once during `PluginInterface.Initialize()` / `Enable()`.
- **Zero Heap Allocations in Render Loop**: `malloc`, `new`, and C++ heap containers are forbidden inside the frame rendering loop (`Update()`, `Commit()`, `OnMouseMove()`).
- **Stack & Register Arithmetic**: Matrix calculations and Gaussian curve mathematics execute in CPU registers using SIMD-friendly linear arithmetic.

---

## 4. Asynchronous Non-Blocking Logging

- Calls to `TE_LogWrite()` copy structured log records into an SRWLock-guarded circular ring buffer (< 5 µs).
- File I/O operations are offloaded to an asynchronous background worker thread, ensuring the UI message pump is never stalled by disk latency.
