# Part 5 — Constraints and Design Decisions

> Technical Reconstruction Specification — TaskbarEngine

---

## 5.1 MUST HAVE (Required Behavior)

1. **Two-process model**: Tray App (EXE) + Engine DLL (in Explorer). No third process.
2. **Pure C17 plugin ABI**: No COM, no C++ at the ABI boundary. Vtable struct with function pointers. `extern "C"` linkage.
3. **JSONC configuration**: Parsed by vendored `cJSON` (~1000 LOC, single C file) + comment stripper. NOT TOML, NOT toml++.
4. **Deferred initialization**: `DllMain` must only store HINSTANCE, set process guard, and `PostMessage`. Real init runs outside the loader lock on the UI thread.
5. **`SetWindowSubclass` for taskbar interception**: Use the safe `comctl32` API, NOT `SetWindowLongPtr(GWLP_WNDPROC)`.
6. **`WH_CBT` hook for injection**: NOT `WH_SHELL`, NOT `AppInit_DLLs`, NOT IAT hooking.
7. **GUI writes config, Engine only reads**: No file locking. Engine detects changes via `ReadDirectoryChangesW`.
8. **Synchronous event dispatch**: Function pointer callbacks, no async queue, no message posting for event delivery.
9. **SEH + watchdog around all plugin callbacks**: Every call into plugin code wrapped in `__try/__except` with a 100 ms watchdog timer.
10. **`Disable()` must fully revert all changes**: Hard contract. Taskbar must return to exact original state.
11. **Named Pipe IPC with restricted ACL**: Current user SID only. Binary protocol with magic number validation.
12. **Explorer crash recovery**: `WaitForSingleObject` on Explorer PID + `TaskbarCreated` message for re-injection.
13. **Plugin priority ordering**: Lower number = loaded first. Enable in ascending, Disable in descending order.
14. **Config hot-reload with diff-based dispatch**: Only affected plugins receive `TE_EVENT_CONFIG_CHANGED`.
15. **Zero idle CPU**: Event-driven architecture with no polling, no background timers when idle.
16. **Self-canceling animation timer**: Frame loop runs ONLY while mouse is in taskbar region + settle.
17. **Per-monitor DPI v2 awareness**: `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`.
18. **Graceful shutdown sequence**: Tray App → SHUTDOWN pipe → Engine disables/shuts down plugins (reverse order) → SHUTDOWN_COMPLETE → UnhookWindowsHookEx → exit.
19. **Lock-free ring buffer logger**: Pre-allocated 64 KB, `InterlockedCompareExchange`, background flush thread.
20. **Absolute DLL path resolution**: `LoadLibraryW` must use full path from `GetModuleFileNameW`, never relative.

---

## 5.2 MUST NOT (Prohibited)

1. **MUST NOT binary-patch `explorer.exe` in memory.** No writing to Explorer's code sections.
2. **MUST NOT hook ntdll/kernel32 functions.** No IAT hooking, no MinHook, no Detours.
3. **MUST NOT use `AppInit_DLLs` registry key.** This is a system-wide injection vector.
4. **MUST NOT call `exit()`, `abort()`, or `TerminateProcess()`.** These kill Explorer.
5. **MUST NOT throw C++ exceptions across the plugin ABI boundary.** All exceptions must be caught and converted to HRESULT.
6. **MUST NOT call `LoadLibrary`, `CreateThread`, or `CoInitializeEx` from `DllMain`.** Loader lock violations.
7. **MUST NOT mutate `TE_CoreState` from background threads.** All state mutations must execute on the UI thread.
8. **MUST NOT allocate heap memory during rendering or event dispatch.** Pre-allocate all buffers.
9. **MUST NOT use polling loops or busy waiting.** Use kernel wait objects.
10. **MUST NOT use `SetWindowLongPtr(GWLP_WNDPROC)` for subclassing.** Use `SetWindowSubclass`.
11. **MUST NOT hardcode file paths, model names, or API endpoints in source code.** Use config or environment.
12. **MUST NOT use global mutable state.** All shared state lives in `TE_CoreState` accessed via `PluginContext`.
13. **MUST NOT hold a lock while calling a plugin callback.** Deadlock risk.
14. **MUST NOT use `alloca` for variable-length data > 1 KB.** Stack overflow risk.
15. **MUST NOT store projects on the C: drive.** User preference: use D: drive.

---

## 5.3 PERFORMANCE (Requirements)

| Metric | Hard Requirement |
|---|---|
| Idle CPU | 0% |
| Average CPU | < 0.5% |
| Peak CPU | < 2% |
| Idle RAM | < 10 MB (all plugins loaded) |
| Plugin load time | < 5 ms per plugin |
| Startup time | < 50 ms total |
| Animation latency | < 2 ms (WM_MOUSEMOVE to Commit) |
| Taskbar redraw | < 1 ms |
| Animation FPS | ≥ 60, target 120 |
| GPU idle | Negligible (zero cost when not animating) |
| Event dispatch | < 10 µs per subscriber |
| Ring buffer write | < 100 ns per entry |
| Config parse | < 1 ms |
| Config diff | < 500 µs |
| UIA tree walk | < 10 ms |
| Icon cache invalidation | < 5 ms |

---

## 5.4 ARCHITECTURE (Rules and Boundaries)

1. **Plugin ABI is frozen after Phase 1.** `PluginInterface` struct layout must never change. New features use `api_version` gating and `struct_size` checks in `PluginContext`.
2. **No feature-specific logic in Core Manager.** The Core Manager is a generic plugin host. All taskbar-specific behavior lives in plugins.
3. **Composition over inheritance.** No class hierarchies deeper than 2 levels.
4. **Every `.c` file must have a corresponding `.h` header.**
5. **All public functions prefixed with `TE_` (SDK) or module name (e.g., `TaskbarResize_`).** 
6. **No C++ features in `.c` files.** No `//` comments in headers shared with C.
7. **Functions must be small and focused — max ~60 lines.** Extract helpers for longer functions.
8. **Include order**: Own header first → system headers → project headers.
9. **Forward-declare structs in headers** instead of including other headers when possible.
10. **Plugin DLLs export exactly one function**: `TE_EXPORT const PluginInterface* GetPluginInterface(void)`.
11. **Plugins must not call `LoadLibrary`, `CreateThread`, or affect Explorer's global state.**
12. **Plugins must not store references to `PluginContext` beyond their own lifetime.**

---

## 5.5 PLATFORM (Windows/Compiler Constraints)

| Constraint | Value |
|---|---|
| Minimum Windows version | Windows 11 22H2 (build 22621) |
| C standard | C17 (`/std:c17`) |
| C++ standard | C++17 (`/std:c++17`) |
| Build system | CMake + Ninja |
| Release compiler | MSVC (Visual Studio 2022+) |
| Debug compiler | Clang-cl (for ASan/UBSan) |
| Warning level | `/W4 /WX` (MSVC), `-Wall -Wextra -Werror` (Clang-cl) |
| Debug builds | AddressSanitizer enabled (`-fsanitize=address`) |
| Release builds | Full optimization (`/O2 /GL /LTCG`) |
| DPI awareness | Per-monitor DPI v2 (manifest + runtime) |
| GUI framework | WinUI 3 / Windows App SDK (C++/WinRT) |
| Distribution | Portable ZIP (no installer, no MSIX) |
| License | MIT |

---

## 5.6 DEPENDENCIES

### Required

| Dependency | Version | Purpose | Integration |
|---|---|---|---|
| `cJSON` | Latest | JSONC config parsing | Vendored in `ThirdParty/cJSON/` |
| `Catch2` | v3.x | Unit testing | CMake FetchContent (dev only) |
| `Google Benchmark` | Latest | Micro-benchmarks | CMake FetchContent (dev only) |
| Windows App SDK | Latest | WinUI 3 GUI hosting | NuGet / system runtime |

### Prohibited

| Dependency | Reason |
|---|---|
| `toml++` / any TOML parser | Replaced by cJSON (design decision) |
| `nlohmann/json` | Too heavy (~22,000 LOC). cJSON is sufficient. |
| `boost` | Too heavy. No Boost dependency. |
| `Qt` | Too heavy. WinUI 3 is the GUI framework. |
| `MinHook` / `Detours` | Prohibited: no function hooking. |
| Rust | Gated: allowed only when benchmarks show >5% improvement. Requires explicit user approval. |

---

## 5.7 Major Design Decisions Record

### Decision 1: Two-Process Model (Not Three)

**Decision**: Tray App + Engine DLL. GUI hosted in-process within the Tray App.

**Reason**: Simpler architecture. GUI is opened on demand, doesn't need its own process. Named pipe IPC handles the Engine ↔ GUI communication.

**Trade-off**: GUI WinUI 3 cold start (~500 ms) adds latency to opening settings.

**Consequence**: Settings GUI is a `DesktopWindow` inside the Tray App, not a separate EXE. Some docs reference a separate `TaskbarEngineSettings.exe` — the design decision document overrides this to in-process hosting.

### Decision 2: Pure C ABI (Not COM)

**Decision**: Plugin interface is a plain C struct of function pointers. NOT `IUnknown`-derived COM.

**Reason**: Simpler, no name mangling, works across compilers, no COM registration, no apartment model complexity.

**Trade-off**: No built-in marshaling, no proxy/stub generation, manual version gating via `struct_size`.

**Consequence**: Every plugin DLL exports `GetPluginInterface()` returning a `PluginInterface*`. No `QueryInterface`, no `AddRef`/`Release` at the ABI level.

### Decision 3: JSONC with cJSON (Not TOML)

**Decision**: Config format is JSONC. Parser is vendored `cJSON` (pure C, ~1000 LOC).

**Reason**: Dramatically smaller than `toml++` (~12,000 LOC, C++17). Zero C++ dependency for config parsing. JSONC comment support via a ~30-line stripper.

**Trade-off**: JSON is more verbose than TOML. No native support for multiline strings.

**Consequence**: Config file is `config.jsonc`. Comment stripping must handle edge cases (comments inside quoted strings).

### Decision 4: DirectComposition Overlay (Not In-Place Hooking)

**Decision**: Icon magnification uses a transparent overlay window with DirectComposition visuals, NOT in-memory function hooking of Explorer's rendering pipeline.

**Reason**: Non-invasive. Doesn't modify Explorer's memory. Uses documented APIs. GPU-accelerated.

**Trade-off**: Overlay visual and real icon hit-test regions are desynchronized. Users may click wrong targets when icons are magnified. Thumbnail previews appear at wrong positions. This is a fundamental limitation of the overlay approach.

**Consequence**: Max magnification scale should be constrained (~1.2x–1.3x) to minimize the visual/interaction desync. Higher scales (1.5x+) create unacceptable UX.

### Decision 5: No Plugin Signature Verification

**Decision**: Trust the user. No signature verification on plugin DLLs.

**Reason**: Simpler. This is a power-user tool, not an enterprise product.

**Trade-off**: A malicious DLL in `Modules/` executes inside Explorer with full user privileges.

**Consequence**: The README must clearly warn users to only use plugins from trusted sources.

### Decision 6: Synchronous Event Dispatch (Not Async Queue)

**Decision**: Events are dispatched by direct function pointer invocation on the UI thread. No async queue.

**Reason**: Simpler. Lower latency (<10 µs). No queue allocation, no threading complexity.

**Trade-off**: A slow plugin callback blocks Explorer's message pump. Mitigated by the 100 ms watchdog.

**Consequence**: All plugin event callbacks MUST complete within 1 ms. Heavy work must be deferred to the next frame or a timer callback.

### Decision 7: WH_CBT Hook Type (Not WH_SHELL)

**Decision**: Use `WH_CBT` for injection, not `WH_SHELL`.

**Reason**: CBT hooks fire on window creation/activation/focus events, which are common enough to trigger DLL load quickly. WH_SHELL has restrictions on which processes receive it.

**Trade-off**: CBT hooks inject into ALL processes that trigger CBT events. The process guard (`IsExplorerProcess`) exits immediately in non-Explorer processes.

**Consequence**: `DisableThreadLibraryCalls(hinstDLL)` must be called in `DllMain` to prevent thread attach/detach overhead in non-Explorer processes.
