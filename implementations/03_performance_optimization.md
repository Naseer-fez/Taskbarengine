# Part 3 — Performance and Low-Level Optimization

> Technical Reconstruction Specification — TaskbarEngine

---

## 3.1 Performance Targets (Hard Requirements)

| Metric | Target | Measurement Method |
|---|---|---|
| Idle CPU | **0%** | `GetProcessTimes` over 60s, mouse stationary |
| Average CPU | **< 0.5%** | `GetProcessTimes` over 60s, typical interaction |
| Peak CPU | **< 2%** | `GetProcessTimes` during animation burst |
| Idle RAM | **< 10 MB** | `GetProcessMemoryInfo` (WorkingSetSize), all plugins loaded |
| Plugin load | **< 5 ms** | `QueryPerformanceCounter` around `LoadLibrary` + init |
| Startup | **< 50 ms** | `QueryPerformanceCounter` from hook install to all plugins enabled |
| Animation latency | **< 2 ms** | `QueryPerformanceCounter` from `WM_MOUSEMOVE` to `Commit()` |
| Taskbar redraw | **< 1 ms** | `QueryPerformanceCounter` around `SetWindowPos` |
| Frame rate | **≥ 60 FPS**, target **120 FPS** | Frame time measurement during animation |
| GPU usage | **Negligible** except during animations | GPU View / PresentMon |

---

## 3.2 CPU Hot Paths

### Hot Path 1: Animation Frame Loop (IconHover)

**Location**: `Modules/icon_hover/frame_loop.cpp`, `magnification.c`

**Frequency**: Every ~8 ms (120 Hz) while mouse is over taskbar.

**Budget**: < 2 ms total per frame.

**Operations per frame**:
1. Read cursor position (`GetCursorPos`) — ~0.001 ms
2. Compute magnification scales for ~20 icons — ~0.05 ms
3. Set `IDCompositionScaleTransform` + `IDCompositionTranslateTransform` per icon — ~0.2 ms
4. `IDCompositionDevice::Commit()` — ~0.5 ms

**Required optimization**:
- Pre-allocate the icon visual array at `Enable()` time. Never allocate/free visuals per frame.
- Cache icon center positions. Only recompute on `TE_EVENT_SHELL_HOOK`.
- Use `float` math, not `double`, for magnification curves.
- Avoid `exp()` in the inner loop by using a pre-computed lookup table for Gaussian (256 entries for u ∈ [0, 1]).

**Recommended optimization**:
- SIMD (SSE2) for batch scale computation across 4 icons at a time. Not required for v1 but provides ~4x speedup on the math.

### Hot Path 2: Event Dispatch

**Location**: `Core/src/event_dispatch.c`

**Frequency**: Every Win32 message that hits the subclass proc (can be thousands per second during mouse movement).

**Budget**: < 10 µs per dispatch.

**Required optimization**:
- The message filter table must use O(1) lookup (bitmap or small hash table for subscribed message IDs).
- Do not iterate the full subscription table for messages no plugin subscribes to.
- SEH frame setup cost is ~0.1 µs per `__try`. This is acceptable.

### Hot Path 3: Subclass Proc

**Location**: `Core/src/taskbar_subclass.c`

**Frequency**: Every Win32 message to `Shell_TrayWnd`.

**Budget**: < 1 µs for unsubscribed messages (just call `DefSubclassProc`).

**Required optimization**:
- First check: is this message in the subscribed set? If not, immediately call `DefSubclassProc`.
- Use a 256-bit bitmap for common message IDs (WM_0 through WM_USER-1) for O(1) lookup.
- For messages in WM_USER+ range, use a small sorted array or hash.

---

## 3.3 GPU Hot Paths

### DirectComposition Commit

**What**: `IDCompositionDevice::Commit()` flushes all pending visual tree changes to DWM for GPU composition.

**Budget**: < 0.5 ms per call.

**Optimization**:
- Batch all `SetTransform` calls before a single `Commit()`. Never commit after each individual transform.
- `Commit()` is the frame boundary. Minimize work between the last `SetTransform` and `Commit()`.
- On integrated GPUs (Intel UHD), rapid commits at 120 Hz add measurable GPU load. Test on integrated graphics specifically.

### Icon Surface Upload

**What**: Uploading captured icon bitmaps into `IDCompositionSurface` textures.

**Budget**: < 2 ms per icon (one-time cost).

**Optimization**:
- Capture icons ONCE at `Enable()` time or on first discovery.
- Never re-capture during animation frames.
- Use `SHIL_JUMBO` (256×256) and let DComp scale down. Avoids repeated capture at different sizes.
- Invalidate and re-capture only on `TE_EVENT_SHELL_HOOK` (app change).

---

## 3.4 Memory-Sensitive Areas

### Ring Buffer Logger

**Allocation**: 64 KB pre-allocated at `TE_LogInit()`. Never grows.

**Rule**: Zero heap allocations during log writes. The ring buffer uses fixed-size entries with `InterlockedCompareExchange` for atomic position advancement.

### Icon Visual Array

**Allocation**: Pre-allocated at plugin `Enable()` time for max expected icons (~32).

**Rule**: Never allocate/free `IDCompositionVisual` objects during the frame loop. Reuse visuals by showing/hiding them.

### Event Dispatch Table

**Allocation**: Fixed-size array of 64 subscription entries. Never grows.

### Plugin Registry

**Allocation**: Fixed-size array of 32 plugin entries. Never grows.

### State Store

**Allocation**: Fixed-capacity hash map of 256 entries. Never grows.

### General Rules

- **No heap allocations during rendering or event dispatch.** All buffers pre-allocated at `Enable()` time.
- **Prefer stack allocation for transient data < 4 KB.**
- **Reuse buffers.** Never allocate per-frame.
- **Zero leaks.** Every `malloc` → `free`. Every `LoadLibrary` → `FreeLibrary`. Every COM `AddRef` → `Release`.

---

## 3.5 Idle CPU Guarantee (0%)

**Problem**: Any timer, polling loop, or background thread that wakes periodically will show non-zero CPU.

**Solution**: Pure event-driven architecture. When idle:

| Thread | State | Wakeup Condition |
|---|---|---|
| Explorer main thread | Blocked in `GetMessage` | Any window message |
| Config watcher | Blocked in `ReadDirectoryChangesW` | File system change |
| IPC server | Blocked in `ConnectNamedPipe` (overlapped) | Pipe connection |
| Log flush | Blocked in `WaitForSingleObject` | Flush event signal |
| Animation timer | **NOT RUNNING** | Mouse enters taskbar |

**Critical rule**: The animation timer (`CreateTimerQueueTimer`) must only exist while the mouse is in the taskbar region. It must self-cancel when:
1. Mouse leaves (`WM_MOUSELEAVE` via `TrackMouseEvent`)
2. AND settle animation completes (all icons at scale 1.0)

**Risk**: If `WM_MOUSELEAVE` is missed (rare edge case with multiple `TrackMouseEvent` calls), the timer runs forever. Mitigation: secondary check — if no `WM_MOUSEMOVE` received for 500 ms, cancel the timer.

---

## 3.6 Idle GPU Guarantee

**Problem**: DirectComposition overlay window consumes GPU resources even when invisible.

**Solution**:
- Set overlay window alpha to 0 when no hover animation is active.
- At alpha=0, DWM skips composition for that visual tree (zero GPU cost).
- Only set alpha > 0 when the animation frame loop starts.
- On settle completion, fade to alpha=0 and stop committing.

---

## 3.7 Frame Timing Strategy

**Approach**: Vsync-locked timer using `DwmFlush()` or `CreateTimerQueueTimer` at monitor refresh interval.

**Timer interval calculation**:
```c
DWM_TIMING_INFO timing;
DwmGetCompositionTimingInfo(NULL, &timing);
float refresh_period_ms = (float)timing.qpcRefreshPeriod / (float)qpc_frequency * 1000.0f;
// Typically 16.67 ms (60 Hz) or 8.33 ms (120 Hz)
```

**Alternative**: `CreateTimerQueueTimer` at ~8 ms (125 Hz cap). Simpler, avoids blocking the timer thread with `DwmFlush`.

**Delta time calculation**:
```c
LARGE_INTEGER now;
QueryPerformanceCounter(&now);
float dt = (float)(now.QuadPart - last_frame.QuadPart) / (float)qpc_frequency;
last_frame = now;
```

---

## 3.8 Startup Time Budget

**Target**: < 50 ms from hook install to all plugins enabled.

| Phase | Budget | Operations |
|---|---|---|
| DllMain Phase A | < 1 ms | Store HINSTANCE, find Shell_TrayWnd, install subclass, PostMessage |
| Phase B: Config parse | < 1 ms | Read + strip comments + cJSON_Parse |
| Phase B: Plugin scan | < 2 ms | FindFirstFileW/FindNextFileW on Modules/*.dll |
| Phase B: Plugin load (×2) | < 10 ms | LoadLibrary + GetProcAddress + GetMetadata × 2 plugins |
| Phase B: Plugin init (×2) | < 10 ms | Initialize + Enable × 2 plugins |
| Phase B: Start watchers | < 1 ms | Config watcher + IPC server threads |
| **Total** | **< 25 ms** | Within 50 ms budget |

---

## 3.9 Memory Footprint Budget

| Component | Estimated Size | Notes |
|---|---|---|
| EngineDLL.dll code/data | ~200 KB | Static, shared with Explorer |
| TE_CoreState | ~4 KB | Singleton struct |
| Config JSON tree | ~10 KB | Typical config |
| Ring buffer | 64 KB | Pre-allocated |
| Plugin registry | ~2 KB | 32 entries × 64 bytes |
| Event dispatch table | ~4 KB | 64 entries × 64 bytes |
| State store | ~16 KB | 256 entries |
| Icon bitmaps (20 icons @ 256×256 RGBA) | ~5 MB | One-time capture |
| DComp visual objects (20 visuals) | ~40 KB | DWM internal tracking |
| Overlay window backing | ~2 MB | 1920×48 layered surface |
| **Total** | **~8-10 MB** | Within 10 MB target |

---

## 3.10 Optimization Classification

### Required Optimizations (Must Implement)

| Optimization | Reason |
|---|---|
| Pre-allocate all buffers at Enable() | Zero per-frame allocation rule |
| Event-driven idle (no polling) | 0% idle CPU requirement |
| Self-canceling animation timer | 0% idle CPU requirement |
| Message filter bitmap | Subclass proc < 1 µs for unsubscribed messages |
| Icon bitmap caching | Never recapture during animation |
| Batch DComp SetTransform before Commit | Minimize GPU overhead |
| Lock-free ring buffer writes | Logger must not block UI thread |
| Process guard in DllMain | Minimize footprint in non-Explorer processes |
| Absolute DLL path resolution | Prevent LoadLibrary failures from CWD dependency |

### Recommended Optimizations (Should Implement)

| Optimization | Reason | Trade-off |
|---|---|---|
| Gaussian lookup table (256 entries) | Avoids `exp()` per icon per frame | ~1 KB memory, slight accuracy loss |
| UIA query throttle (max 2/sec) | Prevents UIA blocking the UI thread | Stale positions for ~500 ms max |
| Overlay alpha=0 when idle | Zero GPU cost when not animating | Extra `Commit()` on transition |
| Timer fallback for missed WM_MOUSELEAVE | Prevents runaway animation loop | Adds one background check |

### Optional Future Optimizations (v2+)

| Optimization | Reason | When to Consider |
|---|---|---|
| SIMD scale computation | 4x math throughput | If >40 icons or if 240 Hz monitors |
| Icon atlas texture | Single DComp surface for all icons | If GPU memory is a concern |
| Thread pool for UIA queries | Remove background thread | If thread count is a concern |
| Profile-guided optimization (PGO) | ~10-20% code path improvement | Release builds |
