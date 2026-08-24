# Part 6 — Reconstruction Phases

> Technical Reconstruction Specification — TaskbarEngine

---

## Phase 1 — Project Foundation + Injection Proof

### Objective

Establish the entire build infrastructure, vendor third-party dependencies, define the plugin ABI headers, create the SDK skeleton, set up the Catch2 test framework, and validate the riskiest technical assumption: that a `WH_CBT` hook successfully injects the Engine DLL into Explorer on Windows 11 22H2+ with deferred `PostMessage`-based initialization.

If injection fails on the target platform, every subsequent phase must be redesigned. This phase retires that risk.

### Implementation

1. **Root CMake workspace**: `CMakeLists.txt` with Ninja generator, MSVC + Clang-cl toolchain presets via `CMakePresets.json`.
2. **Tray App skeleton** (`App/`): Minimal `WinMain`, hidden HWND for message pump, `SetWindowsHookEx(WH_CBT)` targeting Explorer thread, `Shell_NotifyIcon` tray icon (no menu yet).
3. **Engine DLL skeleton** (`Core/`): `DllMain` with process guard (`IsExplorerProcess`) + `DisableThreadLibraryCalls` + `PostMessage(WM_TE_INIT)` deferred init. `InitializeEngine()` finds `Shell_TrayWnd` and logs success via `OutputDebugStringW`.
4. **SDK headers** (`SDK/include/sdk/`): `te_plugin.h` (PluginInterface, PluginMetadata, PluginContext, SettingDescriptor, StateValue), `te_types.h` (TE_EXPORT, TE_API_VERSION, HRESULT macros), `te_log.h` (log level enum + function signatures), `te_jsonc.h` (JSONC parse API).
5. **cJSON vendored**: `ThirdParty/cJSON/cJSON.c` + `cJSON.h`.
6. **JSONC comment stripper**: `SDK/src/te_jsonc.c` — strip `//` and `/* */` comments before `cJSON_Parse`.
7. **Catch2 test project**: `Tests/CMakeLists.txt`, Catch2 via FetchContent, `test_jsonc.cpp` (comment stripping, valid/malformed JSON, nested access).
8. **Default config**: `Config/default_config.jsonc` with example plugin sections.
9. **App manifest**: `App/res/app.manifest` with DPI awareness, COM registration.

### Components

| File | Purpose |
|---|---|
| `CMakeLists.txt` (root) | Workspace definition, sub-projects |
| `CMakePresets.json` | MSVC Release + Clang-cl Debug presets |
| `App/src/main.c` | WinMain, hook install, message pump |
| `App/src/tray.c` | Shell_NotifyIcon |
| `Core/src/dllmain.c` | DllMain + process guard + PostMessage |
| `Core/src/engine_init.c` | InitializeEngine() — finds Shell_TrayWnd |
| `SDK/include/sdk/te_plugin.h` | Plugin ABI (frozen) |
| `SDK/include/sdk/te_types.h` | Core types |
| `SDK/include/sdk/te_log.h` | Log API |
| `SDK/include/sdk/te_jsonc.h` | JSONC parse API |
| `SDK/src/te_jsonc.c` | Comment stripper + cJSON wrapper |
| `ThirdParty/cJSON/cJSON.c` | Vendored JSON parser |
| `Tests/test_jsonc.cpp` | JSONC unit tests |
| `Config/default_config.jsonc` | Default configuration |
| `.gitignore` | Build output exclusions |

### Dependencies

None — this is Phase 1.

### Validation

1. `cmake --build` succeeds on both MSVC and Clang-cl toolchains.
2. `ctest` passes all JSONC parser tests.
3. Running `TaskbarEngine.exe` on Windows 11 22H2+ causes `EngineDLL.dll` to load into `explorer.exe`, log "Engine initialized" via `OutputDebugStringW`, and find `Shell_TrayWnd`.
4. Closing `TaskbarEngine.exe` cleanly unloads the DLL from Explorer (verified via Process Explorer).
5. All ABI types in `te_plugin.h` compile without warnings under `/W4` on both toolchains.

### Performance Check

- `DllMain` completes in < 1 ms.
- `InitializeEngine()` completes in < 10 ms.
- cJSON parsing of default config completes in < 1 ms.
- Engine DLL static footprint in non-Explorer processes is negligible (early exit in DllMain).

### Exit Criteria

1. Dual-toolchain build passes with zero warnings.
2. All JSONC tests pass.
3. DLL injection into Explorer verified on Windows 11 22H2+.
4. Deferred init via PostMessage verified.
5. Clean unload on Tray App exit verified.

### Risks

| Risk | Impact | Mitigation |
|---|---|---|
| `SetWindowsHookEx(WH_CBT)` blocked by antivirus | High | Test with Windows Defender; document AV exclusion |
| `PostMessage` to `Shell_TrayWnd` not processed | High | Verify Shell_TrayWnd exists before posting. Fallback: `CreateTimerQueueTimer` |
| Explorer PPL blocks injection | Critical | Verify PPL status on target builds (not PPL as of 22H2-24H2) |
| DLL loads into many processes | Low | Process guard exits immediately |

---

## Phase 2 — Core Manager + Plugin Lifecycle

### Objective

Build the Core Manager — the brain of the Engine DLL. Implement event dispatch, config parsing with hot-reload, plugin loader with lifecycle management, fault isolation (SEH + watchdog), async logging, and a dummy test plugin that validates the entire pipeline.

### Implementation

1. **Core Manager** (`core_manager.c`): Top-level orchestrator — init, load config, start watcher, load plugins, run, shutdown.
2. **Config system** (`config.c`): Parse `config.jsonc`, extract `"core"` and `"plugin.*"` sections, validate, apply defaults.
3. **Config hot-reload** (`config_watcher.c`): `ReadDirectoryChangesW` + 100 ms debounce + diff-based `CONFIG_CHANGED` dispatch.
4. **Event dispatch** (`event_dispatch.c`): Subscription table `{event_type, callback, plugin_id}`, synchronous dispatch with SEH. Max 64 subscriptions.
5. **Plugin loader** (`plugin_loader.c`): Directory scan, `LoadLibrary` + `GetProcAddress`, priority-sorted registry. Max 32 plugins.
6. **Fault isolation** (`fault_isolation.c`): `__try/__except` wrapper, watchdog timer (100 ms), 3-strike disable policy.
7. **Logging** (`te_log_impl.c`): Lock-free ring buffer (64 KB), `InterlockedCompareExchange`, background flush thread, file rotation.
8. **DPI helper** (`te_dpi.c`): `TE_ScaleDPI(value, dpi)` function.
9. **Taskbar subclass** (`taskbar_subclass.c`): `SetWindowSubclass` on `Shell_TrayWnd`, handle `WM_TE_INIT` and forwarding.
10. **State store stubs**: `PublishState` / `QueryState` as no-ops (real implementation in Phase 4).
11. **Dummy plugin** (`Modules/dummy/`): Full `PluginInterface` impl that logs all lifecycle calls and exercises the API.
12. **Event sources (2 of 7)**: Taskbar subclass + config watcher.

### Components

| File | Purpose |
|---|---|
| `Core/src/core_manager.c` | Top-level orchestration |
| `Core/src/config.c` | JSONC loading, validation, defaults |
| `Core/src/config_watcher.c` | ReadDirectoryChangesW + debounce |
| `Core/src/event_dispatch.c` | Subscription table + sync dispatch |
| `Core/src/plugin_loader.c` | Directory scan, LoadLibrary, lifecycle |
| `Core/src/fault_isolation.c` | SEH wrapper + watchdog timer |
| `Core/src/taskbar_subclass.c` | SetWindowSubclass on Shell_TrayWnd |
| `SDK/src/te_log_impl.c` | Ring buffer + flush thread |
| `SDK/src/te_dpi.c` | DPI scaling utility |
| `SDK/include/sdk/te_events.h` | Event type enum + payloads |
| `Modules/dummy/dummy_plugin.c` | Test plugin |
| `Tests/test_config.cpp` | Config parsing tests |
| `Tests/test_event_dispatch.cpp` | Event subscription/dispatch tests |
| `Tests/test_plugin_loader.cpp` | Plugin lifecycle tests |
| `Tests/test_ring_buffer.cpp` | Logging ring buffer tests |
| `Tests/test_dpi.cpp` | DPI scaling tests |

### Dependencies

- Phase 1: Build system, SDK headers, JSONC parser, DLL injection, Shell_TrayWnd discovery.

### Validation

1. DummyPlugin.dll loaded by Core Manager, receives Initialize, Enable, CONFIG_CHANGED (on hot-reload), Disable, Shutdown in correct order.
2. Config hot-reload dispatches events within 200 ms.
3. Deliberately faulting plugin caught by SEH, disabled, engine continues.
4. Ring buffer logger writes to file without blocking main thread.
5. All unit tests pass under Clang-cl ASan (zero leaks).

### Performance Check

- Event dispatch < 10 µs per subscriber.
- Config parsing < 1 ms.
- Config diff < 500 µs.
- Plugin load < 5 ms per plugin.
- Ring buffer write < 100 ns (lock-free).

### Exit Criteria

1. Full plugin lifecycle verified with DummyPlugin.
2. Hot-reload works within 200 ms.
3. SEH fault isolation catches deliberate crashes.
4. Logger works asynchronously.
5. All tests pass under ASan.

### Risks

| Risk | Impact | Mitigation |
|---|---|---|
| SEH doesn't catch heap corruption | Medium | ASan in debug builds |
| ReadDirectoryChangesW multiple events per save | Low | 100 ms debounce |
| Watchdog false positives on slow init | Medium | Allow extended timeout via PluginContext (future) |

---

## Phase 3 — TaskbarResize Plugin + IPC + Crash Recovery

### Objective

Deliver the first real, user-visible feature (dynamic taskbar resize) along with operational infrastructure (Named Pipe IPC, Explorer crash recovery, graceful shutdown) that makes the system production-usable.

### Implementation

1. **TaskbarResize plugin** (`Modules/taskbar_resize/`): Subclass `Shell_TrayWnd`, intercept `WM_WINDOWPOSCHANGING`, enforce height, update `SPI_SETWORKAREA`, handle multi-monitor, handle DPI changes.
2. **Named pipe server** (`Core/src/ipc_server.c`): Binary protocol server, async overlapped I/O, SHUTDOWN/GET_PLUGIN_LIST/ENABLE/DISABLE/RELOAD commands. All mutations marshaled to UI thread.
3. **Named pipe client** (`App/src/ipc_client.c`): Client for tray menu actions.
4. **Binary IPC protocol** (`Core/src/ipc_protocol.c`): Header struct serialization, message type enum.
5. **Tray context menu** (`App/src/tray_menu.c`): Settings (placeholder), Enable/Disable All, Reload Config, About, Exit.
6. **Explorer crash recovery** (`App/src/crash_recovery.c`): Background thread with `WaitForSingleObject(hExplorer)`, `TaskbarCreated` message handling, re-injection.
7. **Graceful shutdown**: Tray → SHUTDOWN → Engine disables/shuts down → SHUTDOWN_COMPLETE → unhook → exit.
8. **Remaining event sources (5 of 7)**: Shell hook, power, device, pipe, virtual desktop (COM).
9. **Google Benchmark integration**: `bench_event_dispatch.cpp`.

### Components

| File | Purpose |
|---|---|
| `Modules/taskbar_resize/taskbar_resize.c` | Taskbar resize plugin |
| `Core/src/ipc_server.c` | Named pipe server |
| `Core/src/ipc_protocol.c` | Message serialization |
| `Core/src/shell_hook.c` | RegisterShellHookWindow |
| `Core/src/power_device.c` | Power + device notifications |
| `Core/src/vdesktop_notify.cpp` | IVirtualDesktopNotification (COM) |
| `App/src/ipc_client.c` | Named pipe client |
| `App/src/tray_menu.c` | Context menu |
| `App/src/crash_recovery.c` | Explorer PID monitor |
| `SDK/include/sdk/te_ipc.h` | IPC constants and types |
| `Tests/test_ipc_protocol.cpp` | Protocol round-trip tests |
| `Tests/test_taskbar_resize.cpp` | Resize logic tests |
| `Tests/test_crash_recovery.cpp` | State machine tests |
| `Benchmarks/bench_event_dispatch.cpp` | Event dispatch latency |

### Dependencies

- Phase 2: Core Manager, event dispatch, config hot-reload, plugin lifecycle, SEH, logging, subclass.

### Validation

1. User runs TaskbarEngine, sees taskbar resize to configured height, edits config, sees live change, exits cleanly with taskbar restored.
2. Killing Explorer results in automatic re-injection within 2 seconds.
3. IPC shutdown completes without orphaning the DLL.
4. Multi-monitor resize works at different DPIs.
5. Event dispatch benchmark shows < 1 µs per callback.

### Performance Check

- `WM_WINDOWPOSCHANGING` handler < 1 µs.
- Named pipe server does not block Explorer's message pump.
- Crash recovery thread consumes zero CPU while waiting.
- TaskbarResize adds < 100 KB to RSS.

### Exit Criteria

1. Taskbar resize works with hot-reload.
2. Crash recovery works.
3. Clean shutdown verified.
4. Multi-monitor DPI correct.
5. Event dispatch < 1 µs benchmark.

### Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Explorer fights SetWindowPos during DPI changes | Medium | Intercept WM_WINDOWPOSCHANGING to override BEFORE it takes effect |
| SPI_SETWORKAREA affects all apps | Medium | Save/restore original work area in Disable() |
| Explorer restart changes Shell_TrayWnd HWND | Expected | Crash recovery re-discovers HWND |

---

## Phase 4 — IconHover Plugin + Animation Engine

### Objective

Implement macOS-style icon hover magnification. This exercises DirectComposition, UI Automation, icon capture, GPU-accelerated animation, vsync timing, and the shared state store.

### Implementation

1. **IconHover plugin** (`Modules/icon_hover/`): C plugin ABI entry + C++ internals for COM/DComp.
2. **UIA discovery** (`uia_discovery.cpp`): `IUIAutomation` → enumerate taskbar buttons → bounding rects, app IDs.
3. **Icon capture** (`icon_capture.cpp`): `SHGetImageList(SHIL_JUMBO)` + icon bitmap extraction → DComp surface cache.
4. **DComp overlay** (`dcomp_overlay.cpp`): Child window of Shell_TrayWnd, `IDCompositionDevice` + target + root visual + per-icon visuals with scale/translate transforms.
5. **Magnification math** (`magnification.c`): 4 curves (Gaussian, Cubic, Cosine, Linear). Pure C.
6. **Icon layout** (`icon_layout.c`): Coordinate mapping from UIA rects to overlay positions.
7. **Frame loop** (`frame_loop.cpp`): Vsync-locked timer, compute scales → set transforms → Commit(). Self-canceling on mouse leave + settle complete.
8. **State store (real impl)** (`Core/src/state_store.c`): `SRWLock` hash map. `PublishState` / `QueryState` now functional. TaskbarResize publishes height, IconHover queries it.
9. **Message filter table**: `PluginContext.subscribe_message(WM_MOUSEMOVE)` for selective Win32 message dispatch.
10. **Multi-monitor**: Per-monitor overlay + icon set.

### Components

| File | Purpose |
|---|---|
| `Modules/icon_hover/icon_hover.c` | Plugin ABI entry (C) |
| `Modules/icon_hover/magnification.c/.h` | 4 curve functions |
| `Modules/icon_hover/icon_layout.c/.h` | Icon coordinate mapping |
| `Modules/icon_hover/uia_discovery.cpp/.h` | UIA enumeration (C++) |
| `Modules/icon_hover/icon_capture.cpp/.h` | Icon bitmap capture (C++) |
| `Modules/icon_hover/dcomp_overlay.cpp/.h` | DComp visual tree (C++) |
| `Modules/icon_hover/frame_loop.cpp/.h` | Animation timer + commit (C++) |
| `Core/src/state_store.c` | SRWLock hash map |
| `Tests/test_magnification.cpp` | Curve function tests |
| `Tests/test_state_store.cpp` | State publish/query tests |
| `Benchmarks/bench_magnification.cpp` | Curve function benchmark |
| `Benchmarks/bench_state_store.cpp` | Query latency benchmark |

### Dependencies

- Phase 2: Plugin lifecycle, event dispatch, subclass.
- Phase 3: Shell hook events, crash recovery, TaskbarResize state publishing.

### Validation

1. Mouse over taskbar produces smooth icon magnification.
2. All 4 curves produce distinct, correct profiles.
3. Hot-reload of scale/radius/curve/speed_ms takes effect within 200 ms.
4. Zero CPU when mouse is away from taskbar.
5. Magnification math benchmark < 500 ns for 20 icons.
6. Animation runs ≥ 60 FPS on Intel UHD 620.

### Performance Check

- Magnification math: < 1 µs total for 20 icons.
- DComp SetTransform + Commit: < 500 µs per frame.
- UIA tree walk: < 10 ms (one-time).
- Icon cache invalidation: < 5 ms.
- Zero CPU/GPU when idle.

### Exit Criteria

1. Smooth magnification matching macOS Dock behavior.
2. All 4 curves work.
3. Hot-reload works.
4. 0% CPU when mouse away.
5. Math benchmark < 500 ns / 20 icons.
6. ≥ 60 FPS on integrated GPU.

### Risks

| Risk | Impact | Mitigation |
|---|---|---|
| UIA tree differs across Windows builds | High | Fallback matching by name/automation ID pattern |
| DComp fails inside Explorer | Critical | Graceful disable of IconHover |
| Icon bitmaps wrong size/DPI | Medium | Request SHIL_JUMBO (256×256), scale down |
| Overlay visual/click desync | Fundamental | Constrain max scale to 1.2x–1.3x |

---

## Phase 5 — GUI + Benchmarks + CI + Release

### Objective

Transform the engine into a distributable product. Build the WinUI 3 auto-generated settings GUI, set up Azure DevOps CI, run full system benchmarks, write documentation, create the portable ZIP package.

### Implementation

1. **WinUI 3 Settings GUI** (`App/src/gui_main.cpp`, `settings_page.cpp`): Auto-generated from `GetSettings()` metadata via IPC. Type-to-control mapping. NavigationView with per-plugin pages.
2. **About page** (`about_page.cpp`): Version info, live DComp performance stats.
3. **Config I/O**: GUI reads config.jsonc, writes atomically (tmp + MoveFileExW), sends RELOAD_CONFIG.
4. **System benchmark** (`Benchmarks/bench_system.cpp`): Real measurement of idle CPU, RSS, startup time, animation FPS, animation latency. Outputs `benchmark_report.md`.
5. **Azure DevOps Pipeline** (`azure-pipelines.yml`): Build matrix, test, benchmark, Doxygen, publish artifacts.
6. **Doxygen**: Generate API reference from `///` comments.
7. **Documentation**: Complete 00–19 Markdown series.
8. **Packaging** (`Scripts/package.ps1`): Release build → ZIP.
9. **Auto-start** (`App/src/scheduler.c`): Task Scheduler registration.
10. **Uninstall** (`Scripts/uninstall.ps1`): Remove Task Scheduler entry.

### Components

| File | Purpose |
|---|---|
| `App/src/gui_main.cpp` | WinUI 3 window hosting (C++/WinRT) |
| `App/src/settings_page.cpp` | Dynamic settings generation |
| `App/src/about_page.cpp` | About page |
| `App/src/scheduler.c` | Task Scheduler COM |
| `Benchmarks/bench_system.cpp` | System-level benchmark |
| `Scripts/package.ps1` | ZIP packaging |
| `Scripts/uninstall.ps1` | Cleanup |
| `Doxyfile` | Doxygen config |
| `azure-pipelines.yml` | CI pipeline |
| `README.md` | User-facing docs |
| `docs/00-19 series` | Architecture documentation |

### Dependencies

- All Phases 1–4 complete.

### Validation

1. User downloads ZIP, extracts, runs, opens Settings, configures both plugins via GUI, sees changes live.
2. All system benchmark targets met.
3. Azure DevOps pipeline passes: MSVC Release + Clang-cl ASan + all tests + benchmarks.
4. Doxygen generates clean API reference with zero warnings.
5. Full integration test: install → configure → use → restart Explorer → recover → exit → clean state.

### Performance Check

- GUI launch < 500 ms (WinUI 3 cold start).
- GUI does not affect Engine performance.
- All targets from Part 3 verified by system benchmark.

### Exit Criteria

1. Distributable ZIP works end-to-end.
2. All benchmark targets met.
3. CI pipeline green.
4. Doxygen clean.
5. Documentation complete.
6. Full integration test passes.

### Risks

| Risk | Impact | Mitigation |
|---|---|---|
| WinUI 3 hosting in unpackaged app is complex | High | Use Windows App SDK bootstrapper API |
| WinUI 3 cold start slow | Low | Accept it — GUI opened on demand |
| System benchmarks miss a target | Medium | Phase 4 exit criteria should catch most issues |
