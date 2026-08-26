> Source of truth: Extracted from the implementation roadmap.

# Phase 4 — IconHover Plugin + Animation Engine

---

## Purpose

Implement the most technically complex v1 feature: macOS-style icon hover
magnification. This phase exercises DirectComposition, UI Automation, icon
capture, GPU-accelerated animation, vsync timing, and the shared state store.

## High-Level Goal

When the user moves their mouse over the taskbar:

1. Icons near the cursor smoothly scale up (configurable max scale, e.g., 1.3x).
2. Neighboring icons scale proportionally based on distance (Gaussian/Cosine/Linear/Cubic falloff).
3. Animation runs at 60-120 FPS, GPU-accelerated via DirectComposition.
4. When the mouse leaves, icons smoothly settle back to 1.0x scale.
5. Zero CPU/GPU cost when the mouse is not near the taskbar.

## Why This Phase Comes Now

IconHover depends on:
- Core Manager + plugin lifecycle (Phase 2)
- Event dispatch + subclass (Phase 2)
- Working Tray App + crash recovery (Phase 3)
- Shared state store (implemented in this phase, API from Phase 2)

It is the last major engine feature before the GUI and release phase.

## Components Implemented

| Component | Language | Description |
|---|---|---|
| IconHover plugin | C/C++ | `Modules/icon_hover/` — the most complex plugin. C for the plugin ABI entry points, C++ for DirectComposition and UIA COM calls. |
| UIA taskbar discovery | C++ | `IUIAutomation` → enumerate taskbar buttons → extract bounding rects, automation IDs, app associations |
| Icon capture | C++ | `SHGetImageList(SHIL_JUMBO)` → extract icon bitmaps for each taskbar app → cache in memory |
| DirectComposition overlay | C++ | Create child window of `Shell_TrayWnd` (`WS_EX_LAYERED \| WS_EX_TRANSPARENT`), bind `IDCompositionDevice` + `IDCompositionTarget`, create per-icon `IDCompositionVisual` with bitmap surfaces |
| Magnification math | C | 4 curve functions: Gaussian, Cosine, Linear, Cubic. `compute_icon_scales(cursor_x, icons[], count, config)` → `float scales[]` |
| Frame loop | C/C++ | Vsync-locked timer via `DwmFlush`. Runs only while mouse is in taskbar region. On each tick: compute scales → set DComp transforms → `Commit()`. |
| Settle animation | C | When mouse leaves: animate icons back to 1.0x over ~200ms. Self-canceling timer. |
| Shared state store (real impl) | C | `SRWLock`-protected hash map of `{"plugin.key" → StateValue}`. `PublishState` / `QueryState` now functional. TaskbarResize publishes `"taskbar_resize.height"`. IconHover queries it. |
| Multi-monitor support | C/C++ | Per-monitor overlay window + icon set. Events tagged with `HMONITOR`. |

## Folder Structure Additions

```diff
 TaskbarEngine/
+├── Modules/
+│   └── icon_hover/
+│       ├── CMakeLists.txt
+│       ├── icon_hover.c           # Plugin ABI entry points (C)
+│       ├── icon_hover_internal.h  # Internal types + state
+│       ├── uia_discovery.cpp      # UIA taskbar element enumeration
+│       ├── uia_discovery.h
+│       ├── icon_capture.cpp       # IImageList icon extraction + cache
+│       ├── icon_capture.h
+│       ├── dcomp_overlay.cpp      # DirectComposition visual tree
+│       ├── dcomp_overlay.h
+│       ├── magnification.c        # 4 curve functions (pure math, C)
+│       ├── magnification.h
+│       ├── frame_loop.cpp         # Vsync timer + commit loop
+│       └── frame_loop.h
 ├── Core/
 │   ├── src/
+│   │   └── state_store.c          # SRWLock hash map for PublishState/QueryState
 │   └── include/
 │       └── core/
+│           └── state_store.h
 ├── Tests/
+│   ├── test_magnification.cpp     # Curve function tests (pure math)
+│   ├── test_state_store.cpp       # Shared state publish/query tests
+│   └── test_icon_scales.cpp       # Scale computation integration tests
 ├── Benchmarks/
+│   ├── bench_magnification.cpp    # Google Benchmark: curve functions
+│   └── bench_state_store.cpp      # Google Benchmark: query latency
```

## Public APIs Introduced

| Header | API Surface |
|---|---|
| `state_store.h` | `TE_StateStoreInit()`, `TE_StateStoreShutdown()`, `TE_StatePublish(plugin, key, StateValue)`, `TE_StateQuery(plugin, key)` → `StateValue*` |
| IconHover settings | `GetSettings()` returns: `scale` (float, 1.0-2.0, default 1.3), `radius` (int, 40-300, default 120), `curve` (enum: gaussian/cosine/linear/cubic, default gaussian), `speed_ms` (int, 50-500, default 150) |

## Internal Systems Created

| System | Description |
|---|---|
| UIA element cache | Array of `{ RECT bounds, wchar_t app_id[256], int icon_index }` per taskbar button. Refreshed on `TE_EVENT_SHELL_HOOK` (app open/close). |
| Icon texture cache | Map of `{ app_id → HBITMAP (or ID2D1Bitmap for DComp surface) }`. Populated on first discovery, invalidated on app change. |
| DComp visual tree | 1 `IDCompositionTarget` per monitor → 1 root visual → N child visuals (one per icon). Each child has a `IDCompositionScaleTransform` and `IDCompositionTranslateTransform`. |
| Vsync frame loop | `CreateTimerQueueTimer` at ~8ms interval (125 Hz). On tick: if mouse is in taskbar → compute scales → set transforms → `Commit()`. If mouse has left and settle is complete → cancel timer. |
| State store | Fixed-capacity hash map (256 entries). Key = `"plugin_name.key"` (string). Value = `StateValue` (typed union). SRWLock: shared reads, exclusive writes. |

## Dependencies on Previous Phases

| Dependency | Source |
|---|---|
| Plugin lifecycle + PluginContext | Phase 2 |
| Event dispatch (TE_EVENT_SHELL_HOOK for icon list changes) | Phase 2 + Phase 3 |
| Shell_TrayWnd HWND + subclass | Phase 2 |
| Shared state API stubs (now implemented for real) | Phase 2 (API), Phase 4 (impl) |
| TaskbarResize publishes "height" state | Phase 3 |
| Crash recovery (re-init after Explorer restart) | Phase 3 |

## Unit Testing Strategy

- `test_magnification.cpp`: For each curve (Gaussian, Cosine, Linear, Cubic): verify `scale(d=0)` == `max_scale`, `scale(d=radius)` ≈ 1.0, `scale(d>radius)` == 1.0, symmetry around cursor, monotonic decrease.
- `test_state_store.cpp`: Publish int, query int. Publish float, query float. Query non-existent key → default. Multi-threaded concurrent read/write stress test under TSan.
- `test_icon_scales.cpp`: Given a fixed icon layout (N icons at known positions) and a cursor position, verify computed scales match expected values for each curve.
- **Manual integration tests**: Visual verification of animation smoothness on real hardware. Record screen at 120 FPS, analyze frame consistency.

## Performance Considerations

- Magnification math (4 calls to `exp()` or `cos()` per icon per frame) must complete in < 1 μs total for 20 icons.
- DComp `SetTransform` + `Commit` must complete in < 500 μs per frame.
- Frame loop timer must not fire when mouse is away from taskbar (true idle = 0% CPU).
- Icon cache invalidation (on app open/close) must complete in < 5 ms.
- UIA tree walk for initial discovery must complete in < 10 ms.
- DComp overlay window must not consume GPU resources when fully transparent (alpha=0).

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| UIA tree structure differs across Windows 11 builds | High — can't find icons | Implement fallback element matching by name/automation ID pattern. Log when structure differs from expected. |
| DirectComposition fails to create device inside Explorer | Critical — no animation | Fallback: disable IconHover gracefully. This would be a v2 investigation area. |
| Icon bitmaps from `IImageList` have wrong size/DPI | Medium — blurry icons | Request `SHIL_JUMBO` (256×256) and scale down. Handle per-monitor DPI. |
| Mouse tracking across overlay window boundaries | Medium — flickering | `WS_EX_TRANSPARENT` ensures mouse events pass through. Track via subclassed `WM_MOUSEMOVE` on `Shell_TrayWnd`. |
| Vsync lock via `DwmFlush` blocks the calling thread | Low — understood | `DwmFlush` runs on a timer callback thread, not Explorer's main thread. |

## Deliverables

- [ ] IconHover plugin loads and enables without errors
- [ ] UIA discovers taskbar icons and extracts bounding rects
- [ ] Icon bitmaps captured and cached
- [ ] DirectComposition overlay renders icons over the taskbar
- [ ] Mouse hover triggers smooth magnification animation at 60+ FPS
- [ ] All 4 magnification curves implemented and configurable
- [ ] Mouse leave triggers smooth settle animation back to 1.0x
- [ ] Shared state store operational (TaskbarResize.height queried by IconHover)
- [ ] Zero CPU when mouse is away from taskbar
- [ ] Multi-monitor overlay support
- [ ] All unit + micro-benchmark tests pass

## Exit Criteria (Definition of Done)

> **Phase 4 is complete when:**
> 1. Moving the mouse across the taskbar produces smooth icon magnification visually matching macOS Dock behavior.
> 2. All 4 curve types produce visually distinct, correct magnification profiles.
> 3. Changing `scale`, `radius`, `curve`, or `speed_ms` via hot-reload takes effect within 200ms.
> 4. With the mouse away from the taskbar, `Process Explorer` shows 0% CPU for `explorer.exe` attributable to the Engine.
> 5. Magnification math micro-benchmark shows < 500 ns for 20 icons.
> 6. Animation runs at ≥ 60 FPS on a system with integrated GPU (Intel UHD 620 or equivalent).
