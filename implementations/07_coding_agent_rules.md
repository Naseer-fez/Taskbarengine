# Part 7 — Implementation Rules for the Coding Agent

> Technical Reconstruction Specification — TaskbarEngine

---

## 7.1 General Rules

1. **Read the entire specification before modifying the repository.** All 7 parts must be understood before writing code.
2. **Treat the architecture and hard constraints (Part 5) as authoritative.** Do not deviate without explicit user approval.
3. **Implement phases in order (Part 6).** Phase N depends on Phase N-1.
4. **Do not skip prerequisites.** Every phase has explicit dependencies and exit criteria.
5. **Do not invent unspecified functionality.** If a feature is not described in this specification, do not implement it.
6. **Do not redesign working systems without a technical reason.** If something works and passes tests, leave it alone.
7. **Keep dependencies minimal.** Only the dependencies listed in Part 5 Section 5.6 are permitted.
8. **Avoid unnecessary abstractions.** Prefer simple, direct implementations over clever indirection.
9. **Prefer RAII and explicit ownership.** C: goto-cleanup. C++: ComPtr, unique_ptr, RAII wrappers.
10. **Avoid unnecessary background threads.** Target 3-4 Engine threads total. Justify every new thread.
11. **Prefer event-driven mechanisms over continuous polling.** Zero idle CPU is a hard requirement.
12. **Measure performance before making optimization claims.** Use `QueryPerformanceCounter` for micro-benchmarks, `GetProcessTimes` for macro.
13. **Validate each phase before starting the next.** All exit criteria and test cases must pass.
14. **Preserve existing working behavior when modifying the system.** No regressions.
15. **Clearly report blockers or `DECISION REQUIRED` items.** Never silently work around an ambiguity.
16. **Never silently change an architectural decision.** Document the change, explain why, and get approval.

---

## 7.2 Language Rules

### C (Default Language)

- Use **C17** (`/std:c17`) for ALL C code.
- C is the default for: config parsing, plugin loader, logging, utilities, threading, memory management, event dispatch, IPC protocol, plugin implementations.
- Every `.c` file must have a corresponding `.h` header.
- All public functions prefixed with `TE_` (SDK) or module name (e.g., `TaskbarResize_`).
- No C++ features in `.c` files. No `//` comments in headers shared with C (use `/* */`).

### C++ (Only Where C Is Insufficient)

- Use **C++17** (`/std:c++17`).
- C++ is allowed ONLY for: DirectComposition, UI Automation (UIA), COM (`CoInitializeEx`, `ComPtr<T>`), WinUI 3 GUI (C++/WinRT).
- C++ files use `.cpp` extension. Headers shared with C use `extern "C"` guards.
- Avoid: STL containers in hot paths, exceptions across DLL boundaries, RTTI, `dynamic_cast`, deep inheritance.
- Prefer: `ComPtr<T>` for COM pointers, RAII for resource cleanup, `std::optional` for nullable returns.

---

## 7.3 Naming Conventions

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

## 7.4 Code Quality Rules

1. **Every public API function** must have a Doxygen `///` comment documenting purpose, parameters, return value, and thread safety.
2. **Functions must be small and focused** — max ~60 lines. Extract helpers for longer functions.
3. **No code duplication.** If a pattern appears twice, extract it into the SDK.
4. **Headers must use include guards** (`#pragma once` is acceptable for MSVC/Clang-cl).
5. **Forward-declare structs in headers** instead of including other headers when possible.
6. **Every `.c`/`.cpp` file must include its own header first**, then system headers, then project headers.
7. **Code comments explain WHY, not WHAT.** The code should be readable enough to explain what.
8. **TODOs must include a phase reference**: `/* TODO(Phase4): implement settle animation */`.

---

## 7.5 Memory Rules

1. **No heap allocations during rendering or event dispatch.** Pre-allocate at plugin `Enable()` time.
2. **Prefer stack allocation** for transient data < 4 KB.
3. **Reuse buffers.** Never allocate per-frame. Use ring buffers, pools, or pre-sized arrays.
4. **Zero leaks.** Every `malloc` → `free`. Every `LoadLibrary` → `FreeLibrary`. Every COM `AddRef` → `Release`.
5. **Validate with ASan.** All Debug builds must pass AddressSanitizer with zero findings.
6. **Use `_aligned_malloc` / `_aligned_free`** for SIMD-aligned data if needed.
7. **Never use `alloca` for variable-length data > 1 KB.**

---

## 7.6 Threading Rules

1. **Minimize thread count.** Target: 1 main thread + 1 log flush + 1 crash recovery + 1-2 I/O threads. That's it.
2. **No polling loops.** If you need periodic work, use `CreateTimerQueueTimer` and document why.
3. **No busy waiting.** Use `WaitForSingleObject`, `WaitForMultipleObjects`, or event objects.
4. **Prefer asynchronous event callbacks** over spawning threads.
5. **Protect shared data with `SRWLock`** (prefer shared reads, exclusive writes).
6. **Use `InterlockedCompareExchange` / `InterlockedIncrement`** for lock-free atomics.
7. **Never hold a lock while calling a plugin callback.** Deadlock risk.
8. **All IPC state mutations marshal to the UI thread** via `PostMessage(WM_TE_IPC_COMMAND)`.

---

## 7.7 Error Handling Rules

1. **Every Win32 API call must be checked.** Use `GetLastError()` for Win32, `HRESULT` for COM.
2. **Return `HRESULT` from all public C functions.** Use `TE_SUCCEEDED(hr)` / `TE_FAILED(hr)` macros.
3. **Never crash Explorer.** All failures must: log, disable plugin, show tray notification, continue.
4. **No uncaught exceptions.** C++ code at the plugin ABI boundary must catch all exceptions and convert to HRESULT.
5. **SEH wrapping** (`__try/__except`) around every plugin callback invocation.
6. **No `exit()`, `abort()`, `TerminateProcess()`.** These kill Explorer.

---

## 7.8 Build Rules

1. **Every commit must compile** on both MSVC and Clang-cl.
2. **Every commit must pass** all Catch2 tests.
3. **Warning level**: `/W4 /WX` on MSVC, `-Wall -Wextra -Werror` on Clang-cl.
4. **Debug builds**: AddressSanitizer enabled.
5. **Release builds**: Full optimization (`/O2 /GL /LTCG` on MSVC).
6. **No `#pragma warning(disable: ...)` without a comment explaining why.**

---

## 7.9 Testing Rules

1. **Every public API function** must have at least one unit test.
2. **Tests use Catch2** with `extern "C"` for C code.
3. **Test file naming**: `test_<module>.cpp`.
4. **Tests must be deterministic** — no dependency on system state, timing, or network.
5. **Integration tests** that require Explorer injection are marked `[integration]` and excluded from CI.
6. **All tests must pass under ASan** (Clang-cl Debug build).

---

## 7.10 Git Rules

1. **Commit messages**: `<component>: <short description>` (e.g., `Core: implement event dispatch table`).
2. **One logical change per commit.** Don't mix unrelated changes.
3. **Never commit generated files** (build output, Doxygen HTML).
4. **`.gitignore` must exclude**: `build/`, `out/`, `*.obj`, `*.pdb`, `*.dll`, `*.exe`, `*.log`.

---

## 7.11 Configuration Rules

1. **Never hardcode file paths, model names, API endpoints, or environment-specific values in source code.**
2. **Use configuration files (JSONC), environment variables, or command-line arguments.**
3. **Every config value must have a default** defined in the plugin's `SettingDescriptor`.
4. **Every config value must be validated** (min/max/allowed values). Invalid values fall back to defaults with a log warning.
5. **Config hot-reload must not crash or leave the system in a partial state.** Atomic: old config stays active until new config is fully validated.

---

## 7.12 Validation Checklist Per Phase

Before declaring a phase complete, verify:

- [ ] All code compiles on both MSVC and Clang-cl with zero warnings
- [ ] All Catch2 tests pass
- [ ] All Catch2 tests pass under ASan (Clang-cl Debug)
- [ ] No memory leaks detected by ASan
- [ ] Performance benchmarks meet targets from Part 3
- [ ] All exit criteria from Phase spec (Part 6) are satisfied
- [ ] New public APIs have Doxygen comments
- [ ] New code follows naming conventions from Section 7.3
- [ ] No hardcoded paths or values
- [ ] No regressions in previously passing tests
