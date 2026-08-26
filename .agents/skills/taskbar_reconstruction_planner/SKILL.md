---
name: taskbar_reconstruction_planner
description: Use this skill to understand the phase-by-phase implementation roadmap for rebuilding TaskbarEngine.
---
# TaskbarEngine Reconstruction Planner

## How to Use
1. Read the phase roadmap: `d:/CODE/Utlities/Taskbar/implementations/06_reconstruction_phases.md`
2. Work strictly serially — Phase N requires all Phase N-1 exit criteria met.
3. Cite exact Phase number/steps in plans. Track progress in `task.md`.

## Phase Summary

### Phase 1 — Foundation & Injection Proof
- Root CMake workspace + MSVC/Clang-cl presets
- SDK headers: `te_plugin.h`, `te_types.h`, `te_log.h`, `te_jsonc.h`
- Vendored `cJSON` + JSONC comment stripper
- `WH_CBT` hook injection into Explorer → validate with `PostMessage(WM_TE_INIT)`
- **Exit**: Hook loads DLL, deferred init fires on UI thread

### Phase 2 — Core Manager & Plugin Lifecycle
- `TE_CoreState` singleton, two-phase init (Phase A in `DllMain` <1 ms, Phase B via `WM_TE_INIT`)
- `config.c` + `config_watcher.c` (100 ms debounce, atomic swap)
- `event_dispatch.c` (64 subscription slots, SEH-wrapped)
- `plugin_loader.c` (32 plugin slots, scan/init/enable/disable/shutdown)
- `fault_isolation.c` (SEH + 100 ms watchdog, 3-strike disable)
- Ring buffer logger (lock-free atomic CAS, <100 ns writes)
- DPI scaling, dummy test plugin
- **Exit**: Config hot-reload triggers plugin re-enable, fault plugin caught by SEH

### Phase 3 — TaskbarResize + IPC + Crash Recovery
- `taskbar_resize` plugin: intercept `WM_WINDOWPOSCHANGING`, clamp height 24–72 px, `SPI_SETWORKAREA`
- Named Pipe IPC (`\\.\pipe\TaskbarEngine`): binary protocol, DACL-secured, overlapped I/O
- Tray App: system tray icon, context menu, IPC client
- Crash recovery state machine: `RUNNING` → `EXPLORER_DEAD` → `WAITING_TASKBAR_CREATED` → `REHOOKING`
- Shell hook, power/device event sources
- **Exit**: Resize persists through config changes, crash recovery re-hooks within 2 s

### Phase 4 — IconHover + Animation Engine
- `icon_hover` plugin: UIA button discovery (throttled ≤2/s, <10 ms), Jumbo icon cache (256×256)
- DirectComposition overlay: layered transparent child of `Shell_TrayWnd`
- Magnification curves: Gaussian, Cosine, Linear, Cubic. Max scale ~1.2–1.35×
- Vsync frame loop with self-canceling timer (active only during hover + 150 ms settle)
- Real `StateStore` (`PublishState`/`QueryState`, 256 entries, SRWLock)
- **Exit**: Smooth ≥60 FPS magnification, 0% CPU when not hovering

### Phase 5 — GUI + Benchmarks + CI + Release
- WinUI 3 Settings GUI (auto-generated from `GetSettings()` descriptors)
- Benchmark suite: event dispatch, config parse, easing, magnification, state store, system profile
- Azure Pipelines CI (4-config matrix)
- Doxygen docs, Task Scheduler auto-logon, `package.ps1` portable ZIP
- **Exit**: All tests pass, benchmarks meet budgets, packaged release
