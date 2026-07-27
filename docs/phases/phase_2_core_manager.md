> Source of truth: Extracted from the implementation roadmap.

# Phase 2 — Core Manager + Plugin Lifecycle

---

## Purpose

Build the Core Manager — the brain of the Engine DLL. This phase implements
the event dispatch infrastructure, config parsing and hot-reload, the plugin
loader (discovery, lifecycle, fault isolation), the async logging system, and a
dummy test plugin that validates the entire pipeline end-to-end.

## High-Level Goal

A working Core Manager that can:

1. Parse `config.jsonc` and extract per-plugin config sub-objects.
2. Watch the config directory and hot-reload on changes (with 100ms debounce).
3. Scan `Modules/` for plugin DLLs, load them, call `GetPluginInterface()`.
4. Call `Initialize()` → `Enable()` on enabled plugins, `Disable()` → `Shutdown()` on exit.
5. Dispatch events to subscribed plugin callbacks synchronously.
6. Wrap all plugin callbacks in SEH + watchdog.
7. Log to a lock-free ring buffer with async background flush.
8. Load and run a `DummyPlugin.dll` that logs lifecycle events.

## Why This Phase Comes Now

The Core Manager depends on the build system, SDK headers, JSONC parser, and
injection mechanism — all delivered in Phase 1. Everything after Phase 2
(plugins, IPC, GUI) depends on the Core Manager. This is the critical
infrastructure phase.

## Components Implemented

| Component | Language | Description |
|---|---|---|
| Config system | C | Parse `config.jsonc`, extract `"core"` and `"plugin.*"` sections, validate required fields, apply in-memory defaults for missing values |
| Config hot-reload | C | `ReadDirectoryChangesW` watcher on config directory, 100ms debounce via `CreateTimerQueueTimer`, diff + dispatch `CONFIG_CHANGED` event to affected plugins |
| Event dispatch | C | Subscription table (`{event_type, callback, plugin_id}`), synchronous dispatch with SEH wrapping, `TE_Subscribe()` / `TE_Unsubscribe()` API |
| Event sources (2 of 7) | C/C++ | 1. `SetWindowSubclass` on `Shell_TrayWnd` (intercepts WM messages) 2. Config file watcher |
| Plugin loader | C | `Modules/` directory scan, `LoadLibrary` + `GetProcAddress("GetPluginInterface")`, `GetMetadata()` for registration, priority-sorted enable |
| Plugin lifecycle | C | `Initialize(ctx)` → `Enable()` → `Update()` → `Disable()` → `Shutdown()`. Clean error handling at each step. |
| Fault isolation | C | `__try/__except` around every plugin callback. Watchdog timer (100ms) per callback via `CreateTimerQueueTimer`. N-strike disable policy. |
| Logging | C | Lock-free SPSC ring buffer (64KB, pre-allocated), `InterlockedCompareExchange` atomic writes, background flush thread to file / `OutputDebugStringW`. Configurable via `core.log_level` and `core.log_to_file`. |
| Shared state API (stubs) | C | `publish_state` and `query_state` function pointers in `PluginContext` — implemented as no-ops returning defaults. Real implementation deferred to Phase 4. |
| Dummy plugin | C | `Modules/dummy/dummy_plugin.c` — implements full `PluginInterface`, logs all lifecycle calls, subscribes to `CONFIG_CHANGED`, exercises the API surface |

## Folder Structure Additions

```diff
 TaskbarEngine/
 ├── Core/
 │   ├── src/
+│   │   ├── core_manager.c        # Top-level init/shutdown orchestration
+│   │   ├── config.c              # JSONC loading, validation, defaults
+│   │   ├── config_watcher.c      # ReadDirectoryChangesW + debounce
+│   │   ├── event_dispatch.c      # Subscription table + sync dispatch
+│   │   ├── plugin_loader.c       # Directory scan, LoadLibrary, lifecycle
+│   │   ├── fault_isolation.c     # SEH wrapper, watchdog timer
+│   │   └── taskbar_subclass.c    # SetWindowSubclass on Shell_TrayWnd
 │   └── include/
 │       └── core/
+│           ├── core_manager.h
+│           ├── config.h
+│           ├── config_watcher.h
+│           ├── event_dispatch.h
+│           ├── plugin_loader.h
+│           ├── fault_isolation.h
+│           └── taskbar_subclass.h
 ├── SDK/
 │   ├── include/
 │   │   └── sdk/
+│   │       ├── te_log_impl.h     # Ring buffer internals
+│   │       ├── te_events.h       # Event type enum, event structs
+│   │       └── te_dpi.h          # TE_ScaleDPI() helper
 │   └── src/
+│       ├── te_log_impl.c         # Ring buffer + flush thread
+│       └── te_dpi.c              # DPI scaling utility
+├── Modules/
+│   └── dummy/
+│       ├── CMakeLists.txt         # DummyPlugin.dll target
+│       └── dummy_plugin.c        # Full PluginInterface impl for testing
 ├── Tests/
+│   ├── test_config.cpp           # Config parsing + validation tests
+│   ├── test_event_dispatch.cpp   # Subscription + dispatch tests
+│   ├── test_plugin_loader.cpp    # Plugin lifecycle mock tests
+│   ├── test_ring_buffer.cpp      # Logging ring buffer tests
+│   └── test_dpi.cpp              # DPI scaling tests
```

## Public APIs Introduced

| Header | API Surface |
|---|---|
| `te_events.h` | `TE_EventType` enum (`TE_EVENT_CONFIG_CHANGED`, `TE_EVENT_DISPLAY_CHANGED`, `TE_EVENT_DPI_CHANGED`, `TE_EVENT_TASKBAR_GEOMETRY`, `TE_EVENT_SHELL_HOOK`, `TE_EVENT_POWER`, `TE_EVENT_DEVICE`, `TE_EVENT_VDESKTOP`), event payload structs (`TE_ConfigChangedEvent`, `TE_DisplayChangedEvent`, etc.) |
| `te_dpi.h` | `TE_ScaleDPI(int value, uint32_t dpi)` → `int` |
| `te_log_impl.h` | Internal: `TE_LogInit()`, `TE_LogShutdown()`, `TE_LogWrite(level, fmt, ...)` |

## Internal Systems Created

| System | Description |
|---|---|
| Core Manager | Top-level orchestrator: init → load config → start watcher → load plugins → run → shutdown |
| Event dispatch table | Fixed-size array of `{event_type, callback_fn, plugin_id}` entries. Max 64 subscriptions. |
| Plugin registry | Array of `{PluginInterface*, PluginMetadata*, HMODULE, enabled, fault_count}` sorted by priority. Max 32 plugins. |
| Ring buffer logger | 64KB pre-allocated circular buffer. SPSC (single producer, single consumer with CAS for thread safety). Background thread drains to file every 100ms or on flush signal. |
| Config diff engine | Compares old vs. new `cJSON` trees. Emits `CONFIG_CHANGED` events only for plugins whose sub-objects changed. |
| Watchdog timer | Per-callback `CreateTimerQueueTimer`. If callback doesn't return within 100ms, sets a flag. After 3 consecutive timeouts, plugin is disabled. |

## Dependencies on Previous Phases

| Dependency | Source |
|---|---|
| CMake build system | Phase 1 |
| SDK headers (`te_plugin.h`, `te_types.h`, `te_log.h`) | Phase 1 |
| JSONC parser (`te_jsonc.h` / `cJSON`) | Phase 1 |
| DLL injection + deferred init | Phase 1 |
| `Shell_TrayWnd` HWND discovery | Phase 1 |

## Unit Testing Strategy

- `test_config.cpp`: Parse valid config, parse config with missing fields (verify defaults), parse malformed JSON (verify error), parse config with unknown plugin sections (verify ignored).
- `test_event_dispatch.cpp`: Subscribe callback, fire event, verify callback received it. Subscribe 2 plugins, verify dispatch order matches priority. Unsubscribe, verify no longer called.
- `test_plugin_loader.cpp`: Load DummyPlugin.dll, verify `GetMetadata()` returns expected values, verify lifecycle sequence (`Initialize` → `Enable` → `Disable` → `Shutdown`).
- `test_ring_buffer.cpp`: Write N entries, read N entries, verify FIFO order. Overflow test: write more than capacity, verify oldest dropped. Multi-threaded write stress test.
- `test_dpi.cpp`: Verify `TE_ScaleDPI(48, 96)` == 48, `TE_ScaleDPI(48, 144)` == 72, `TE_ScaleDPI(48, 192)` == 96.

## Performance Considerations

- Event dispatch must complete in < 10 μs per subscriber (just a function pointer call + SEH frame setup).
- Config parsing must complete in < 1 ms for a typical config file.
- Config diff must complete in < 500 μs.
- Plugin load (`LoadLibrary` + `GetPluginInterface` + `GetMetadata`) must complete in < 5 ms per plugin.
- Ring buffer write must be lock-free and complete in < 100 ns per entry.
- Ring buffer flush thread must sleep when no entries are pending (zero CPU when idle).

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| SEH doesn't catch all fault types (e.g., heap corruption) | Medium — plugin can still crash Explorer | Document limitation. Heap corruption is caught during development via ASan (Clang-cl debug builds). |
| `ReadDirectoryChangesW` fires multiple events per save (editor write patterns) | Low — redundant reloads | 100ms debounce timer coalesces rapid events. |
| Watchdog timer fires during legitimate long operations (e.g., first-time icon cache build) | Medium — false positive disable | Allow plugins to temporarily extend the watchdog timeout via a `PluginContext` API (future). |
| Plugin DLL compiled with different compiler/settings than Engine | Medium — ABI mismatch | Pure C ABI with fixed struct layout. Document required compiler flags. |

## Deliverables

- [ ] Core Manager initializes inside Explorer, parses config, and logs status
- [ ] Config hot-reload detects changes and dispatches events to plugins
- [ ] Plugin loader scans Modules/, loads DummyPlugin.dll, runs full lifecycle
- [ ] SEH + watchdog protects against plugin faults
- [ ] Ring buffer logger works asynchronously with file output
- [ ] `SetWindowSubclass` on Shell_TrayWnd intercepts and forwards messages
- [ ] All Catch2 tests pass on both MSVC and Clang-cl

## Exit Criteria (Definition of Done)

> **Phase 2 is complete when:**
> 1. `DummyPlugin.dll` is loaded by the Core Manager, receives `Initialize`, `Enable`, `CONFIG_CHANGED` (on hot-reload), `Disable`, and `Shutdown` calls in correct order.
> 2. Modifying `config.jsonc` while the engine is running triggers a hot-reload within 200ms and dispatches `CONFIG_CHANGED` only to affected plugins.
> 3. A deliberately faulting plugin (access violation in `Enable()`) is caught by SEH, disabled, and the engine continues running.
> 4. The ring buffer logger writes to a log file without blocking the main thread.
> 5. All unit tests pass under Clang-cl with AddressSanitizer enabled (zero leaks, zero UB).
