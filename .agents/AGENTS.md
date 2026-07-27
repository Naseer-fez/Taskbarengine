# TaskbarEngine — AI Coding Rules

> These rules are mandatory for all code generation in this project.
> Violations must be flagged and corrected before any code is committed.

---

## 1. Source of Truth

- The **authoritative architecture document** is `docs/design_decisions.md`.
- The **implementation roadmap** is in `docs/phases/`.
- Always read these before implementing any component.
- Never redesign or override architectural decisions without explicit user approval.

---

## 2. Language Rules

### C (Default Language)

- Use **C17** (`/std:c17`) for all C code.
- C is the default for: config parsing, plugin loader, logging, utilities, threading, memory management, event dispatch, IPC protocol, plugin implementations.
- Every `.c` file must have a corresponding `.h` header.
- All public functions must be prefixed with `TE_` (SDK) or the module name (e.g., `TaskbarResize_`).
- No C++ features in `.c` files. No `//` comments in headers shared with C (use `/* */`).

### C++ (Only Where C Is Insufficient)

- Use **C++17** (`/std:c++17`).
- C++ is allowed ONLY for: DirectComposition, UI Automation (UIA), COM (`CoInitializeEx`, `ComPtr<T>`), WinUI 3 GUI (C++/WinRT).
- C++ files use `.cpp` extension. Headers shared with C use `extern "C"` guards.
- Avoid: STL containers in hot paths, exceptions across DLL boundaries, RTTI, `dynamic_cast`, deep inheritance hierarchies.
- Prefer: `ComPtr<T>` for COM pointers, RAII for resource cleanup, `std::optional` for nullable returns.

### Rust

- NOT allowed unless benchmarks demonstrate >5% improvement in CPU, memory, startup, or binary size.
- Requires explicit user approval before any Rust code is introduced.

---

## 3. Naming Conventions

| Element | Convention | Example |
|---|---|---|
| Functions (C) | `PascalCase` with module prefix | `TE_LogWrite()`, `TE_JsoncParse()` |
| Functions (C++) | `PascalCase` | `CreateOverlayWindow()` |
| Structs / Types | `PascalCase` | `PluginInterface`, `PluginContext` |
| Enums | `UPPER_SNAKE_CASE` with prefix | `TE_EVENT_CONFIG_CHANGED` |
| Enum types | `PascalCase` | `TE_EventType`, `SettingType` |
| Macros / Constants | `UPPER_SNAKE_CASE` | `TE_API_VERSION`, `TE_LOG_DEBUG` |
| Local variables | `snake_case` | `icon_count`, `cursor_x` |
| Global variables | Forbidden (use accessors) | — |
| Struct members | `snake_case` | `api_version`, `taskbar_hwnd` |
| File names | `snake_case` | `plugin_loader.c`, `event_dispatch.h` |
| Plugin DLL exports | `PascalCase` | `GetPluginInterface` |

---

## 4. Code Structure Rules

- Every public API function **must** have a Doxygen `///` comment documenting purpose, parameters, return value, and thread safety.
- Functions must be **small and focused** — max ~60 lines. Extract helpers for anything longer.
- **No global mutable state.** All shared state lives in the Core Manager and is accessed via `PluginContext` function pointers.
- **Composition over inheritance.** No class hierarchies deeper than 2 levels.
- **No code duplication.** If a pattern appears twice, extract it into the SDK.
- Headers must use **include guards** (`#pragma once` is acceptable for MSVC/Clang-cl).
- Forward-declare structs in headers instead of including other headers when possible.
- Every `.c`/`.cpp` file must include its own header first, then system headers, then project headers.

---

## 5. Memory Rules

- **No heap allocations during rendering or event dispatch.** Pre-allocate all buffers at plugin `Enable()` time.
- **Prefer stack allocation** for transient data < 4 KB.
- **Reuse buffers.** Never allocate per-frame. Use ring buffers, pools, or pre-sized arrays.
- **Zero leaks.** Every `malloc` has a corresponding `free`. Every `LoadLibrary` has a `FreeLibrary`. Every COM `AddRef` has a `Release` (or use `ComPtr`).
- **Validate with ASan.** All Debug builds must pass AddressSanitizer with zero findings.
- Use `_aligned_malloc` / `_aligned_free` for SIMD-aligned data if needed.
- Never use `alloca` for variable-length data > 1 KB.

---

## 6. Threading Rules

- **Minimize thread count.** Target: 1 main thread (Explorer's message pump), 1 log flush thread, 1 crash recovery thread. That's it for the Engine.
- **No polling loops.** If you need periodic work, use `CreateTimerQueueTimer` and document why.
- **No busy waiting.** Use `WaitForSingleObject`, `WaitForMultipleObjects`, or event objects.
- **Prefer asynchronous event callbacks** over spawning threads.
- Protect shared data with `SRWLock` (prefer shared reads, exclusive writes).
- Use `InterlockedCompareExchange` / `InterlockedIncrement` for lock-free atomics.
- Never hold a lock while calling a plugin callback (risk of deadlock).

---

## 7. Error Handling Rules

- **Every Win32 API call must be checked.** Use `GetLastError()` for Win32, `HRESULT` for COM.
- Return `HRESULT` from all public C functions. Use `TE_SUCCEEDED(hr)` / `TE_FAILED(hr)` macros.
- **Never crash Explorer.** All failures must:
  - Log the error with context (function name, error code, affected plugin).
  - Disable the offending plugin gracefully.
  - Show a tray balloon notification to the user.
  - Continue running.
- **No uncaught exceptions.** C++ code at the plugin ABI boundary must catch all exceptions and convert to `HRESULT`.
- **SEH wrapping** (`__try/__except`) around every plugin callback invocation. Catch `EXCEPTION_ACCESS_VIOLATION`, `EXCEPTION_STACK_OVERFLOW`, etc.
- **No `exit()`, `abort()`, `TerminateProcess()`.** These would kill Explorer.

---

## 8. Plugin ABI Rules

- The plugin ABI is defined in `SDK/include/sdk/te_plugin.h`. **It is frozen after Phase 1.**
- All plugin entry points use **C linkage** (`extern "C"`). No name mangling.
- The `PluginInterface` struct layout must never change. New functions are added via `api_version` gating in `PluginContext`.
- Every plugin DLL exports exactly one function: `__declspec(dllexport) const PluginInterface* GetPluginInterface(void)`.
- Plugins must not call `LoadLibrary`, `CreateThread`, or any function that would affect Explorer's global state.
- Plugins must not store references to the `PluginContext` pointer beyond their own lifetime.
- `Disable()` must **fully revert** all changes. This is a hard contract.
- `Shutdown()` must free all resources. Zero leaks after shutdown.

---

## 9. Windows API Policy

### Preference Order

1. Win32 API (documented)
2. COM (documented interfaces)
3. Windows App SDK
4. DirectComposition
5. DWM (`DWMAPI`)
6. DirectX 11/12

### Undocumented API Rules

- Allowed ONLY when no supported alternative exists.
- Must be clearly documented in code comments: WHY no alternative exists.
- Must be isolated behind a wrapper function in the SDK.
- Must gracefully degrade if the API is unavailable or fails at runtime.
- Must be tested on every supported Windows build (22H2, 23H2, 24H2).

### Forbidden

- Binary patching of `explorer.exe` in memory.
- Writing to Explorer's code sections.
- Hooking ntdll/kernel32 functions.
- `AppInit_DLLs` registry key.

---

## 10. Performance Rules

- **Idle CPU must be 0%.** No timers, no polling, no background work when the user is not interacting.
- **Event handlers must complete in < 1 ms.** If work takes longer, defer to a timer or thread pool task.
- **Animation frame loop** runs ONLY when the mouse is in the taskbar region. It must self-cancel when the mouse leaves and the settle animation completes.
- **DComp `Commit()` is the frame boundary.** Minimize work between the last `SetTransform` call and `Commit()`.
- **Profile before optimizing.** Use `QueryPerformanceCounter` for micro-benchmarks, `GetProcessTimes` for macro.
- All hot-path functions must have Google Benchmark micro-benchmarks.

---

## 11. Build Rules

- **Every commit must compile** on both MSVC and Clang-cl.
- **Every commit must pass** all Catch2 tests.
- Use `/W4` warning level on MSVC. Treat warnings as errors (`/WX`) in CI.
- Use `-Wall -Wextra -Werror` on Clang-cl.
- Debug builds: AddressSanitizer enabled (`-fsanitize=address`).
- Release builds: full optimization (`/O2 /GL /LTCG` on MSVC).
- No `#pragma warning(disable: ...)` without a comment explaining why.

---

## 12. Configuration Rules

- Config format is **JSONC** (`config.jsonc`). Parsed by `cJSON` + comment stripper.
- **GUI writes config, Engine only reads.** Never write to config from the Engine.
- Every config value must have a **default** defined in the plugin's `SettingDescriptor`.
- Every config value must be **validated** (min/max/allowed values). Invalid values fall back to defaults with a log warning.
- Config hot-reload must not crash or leave the system in a partial state. Atomic: old config stays active until new config is fully validated.

---

## 13. Logging Rules

- Use the `TE_LogWrite(level, fmt, ...)` API. Never use `printf`, `OutputDebugStringA` directly (except in DllMain before logging is initialized).
- Log levels: `TE_LOG_DEBUG`, `TE_LOG_INFO`, `TE_LOG_WARN`, `TE_LOG_ERROR`.
- **Debug logs are compiled out** in Release builds via `#ifdef TE_DEBUG`.
- Every plugin lifecycle event must be logged at `INFO` level.
- Every error must be logged at `ERROR` level with: function name, error code, and context.
- **Never log PII**: no window titles, no file paths, no user names unless explicitly opted in.

---

## 14. Testing Rules

- Every public API function must have at least one unit test.
- Tests use **Catch2** with `extern "C"` for C code.
- Test file naming: `test_<module>.cpp` (e.g., `test_config.cpp`, `test_magnification.cpp`).
- Tests must be deterministic — no dependency on system state, timing, or network.
- Integration tests that require Explorer injection are marked `[integration]` and excluded from CI.
- All tests must pass under ASan (Clang-cl Debug build).

---

## 15. File Organization Rules

```
Core/           → Engine DLL source (Core Manager, event dispatch, plugin loader, IPC server)
App/            → Tray App source (hook install, tray icon, IPC client, GUI, crash recovery)
SDK/            → Shared SDK (te_plugin.h, utilities, Win32 wrappers, logging)
Modules/<name>/ → One directory per plugin (each builds to a separate DLL)
Tests/          → Catch2 unit tests
Benchmarks/     → Google Benchmark micro-benchmarks
Config/         → Default config.jsonc and examples
Docs/           → Hand-written Markdown documentation
Scripts/        → Build, packaging, CI helper scripts
ThirdParty/     → Vendored dependencies (cJSON)
```

- Each component has its own `CMakeLists.txt`.
- Headers go in `include/<component>/`. Source goes in `src/`.
- Plugin directories are self-contained — each can compile independently.

---

## 16. Git Rules

- Commit messages: `<component>: <short description>` (e.g., `Core: implement event dispatch table`).
- One logical change per commit. Don't mix unrelated changes.
- Never commit generated files (build output, Doxygen HTML).
- `.gitignore` must exclude: `build/`, `out/`, `*.obj`, `*.pdb`, `*.dll`, `*.exe`, `*.log`.

---

## 17. Documentation Rules

- Every public header must have Doxygen `///` comments on all functions, structs, enums, and macros.
- The hand-written Markdown docs (00-19 series) are written in Phase 5. Do not write them during implementation phases.
- Code comments explain **why**, not **what**. The code itself should be readable enough to explain what.
- TODOs must include a name or phase reference: `/* TODO(Phase4): implement settle animation */`.
