> Source of truth: Extracted from the implementation roadmap.

# Phase 1 — Project Foundation + Injection Proof

---

## Purpose

Establish the entire build infrastructure, vendor third-party dependencies,
define the plugin ABI headers, create the SDK skeleton, set up the Catch2 test
framework, and **validate the riskiest technical assumption**: that a
`WH_CBT` hook successfully injects the Engine DLL into Explorer on
Windows 11 22H2+ with deferred `PostMessage`-based initialization.

If injection fails on the target platform, every subsequent phase must be
redesigned. This phase retires that risk.

## High-Level Goal

A compilable, testable project that can:

1. Build a Tray App EXE and an Engine DLL with CMake + Ninja + MSVC.
2. Inject the Engine DLL into Explorer via `SetWindowsHookEx(WH_CBT)`.
3. Detect that the DLL is running inside Explorer (`IsExplorerProcess` guard).
4. Defer initialization via `PostMessage` (outside the loader lock).
5. Find `Shell_TrayWnd` from inside Explorer.
6. Log "Engine initialized inside Explorer" to `OutputDebugStringW`.
7. Cleanly unload when the Tray App exits.

## Why This Phase Comes Now

This is the natural starting point — you cannot build anything without a build
system, and you should not build anything complex before proving the injection
mechanism works on the target platform. Combining both into Phase 1 delivers
maximum value per unit of effort.

## Components Implemented

| Component | Language | Description |
|---|---|---|
| CMake root project | CMake | Top-level `CMakeLists.txt` with sub-projects, Ninja generator, MSVC + Clang-cl toolchain files |
| `TaskbarEngine.exe` (skeleton) | C | Minimal Tray App: `WinMain`, `SetWindowsHookEx(WH_CBT)`, hidden HWND for message pump, `Shell_NotifyIcon` tray icon (no menu yet) |
| `EngineDLL.dll` (skeleton) | C | `DllMain` with process guard + `PostMessage` deferred init. `InitializeEngine()` finds `Shell_TrayWnd` and logs success. |
| SDK headers | C | `te_plugin.h` (PluginInterface, PluginMetadata, PluginContext, SettingDescriptor, StateValue structs), `te_types.h` (common types), `te_log.h` (log level enum + function signatures) |
| `cJSON` vendored | C | `ThirdParty/cJSON/cJSON.c` + `cJSON.h` copied into the tree |
| JSONC comment stripper | C | `SDK/src/te_jsonc.c` — strip `//` comments before passing to `cJSON` |
| Catch2 test project | C++ | `Tests/CMakeLists.txt`, Catch2 fetched via CMake `FetchContent`, one test file verifying cJSON + comment stripper |

## Folder Structure Additions

```
TaskbarEngine/
├── CMakeLists.txt                  # Root CMake
├── cmake/
│   ├── toolchain-msvc.cmake
│   └── toolchain-clang-cl.cmake
├── Core/
│   ├── CMakeLists.txt              # EngineDLL.dll target
│   ├── src/
│   │   ├── dllmain.c              # DllMain + guard + PostMessage
│   │   └── engine_init.c          # InitializeEngine() — finds Shell_TrayWnd
│   └── include/
│       └── core/
│           └── engine.h
├── App/
│   ├── CMakeLists.txt              # TaskbarEngine.exe target
│   ├── src/
│   │   ├── main.c                 # WinMain, message pump, hook install
│   │   └── tray.c                 # Shell_NotifyIcon
│   ├── res/
│   │   ├── app.manifest           # DPI awareness, COM, version
│   │   └── app.rc                 # Icon resource
│   └── include/
│       └── app/
│           └── tray.h
├── SDK/
│   ├── CMakeLists.txt              # Static library target
│   ├── include/
│   │   └── sdk/
│   │       ├── te_plugin.h        # Plugin ABI (PluginInterface, PluginContext, etc.)
│   │       ├── te_types.h         # Common types, HRESULT helpers
│   │       ├── te_log.h           # Log levels, LogFunc typedef
│   │       └── te_jsonc.h         # JSONC parse API
│   └── src/
│       └── te_jsonc.c             # Comment stripper + cJSON wrapper
├── ThirdParty/
│   └── cJSON/
│       ├── cJSON.c
│       └── cJSON.h
├── Tests/
│   ├── CMakeLists.txt
│   └── test_jsonc.cpp             # Catch2 tests for JSONC parsing
├── Config/
│   └── default_config.jsonc       # Default config file (example)
├── Modules/                        # Empty — plugins come in Phase 2+
├── Docs/                           # Empty — Markdown docs come in Phase 5
├── Benchmarks/                     # Empty — benchmarks come in Phase 3+
├── Scripts/                        # Empty — build/package scripts come later
└── .gitignore
```

## Public APIs Introduced

| Header | API Surface |
|---|---|
| `te_plugin.h` | `PluginInterface` struct, `PluginMetadata` struct, `PluginContext` struct, `SettingDescriptor` struct, `SettingType` enum, `StateValue` struct, `LogFunc` / `SubscribeFunc` / etc. typedefs |
| `te_types.h` | `TE_EXPORT` / `TE_IMPORT` macros, `TE_API_VERSION`, HRESULT helper macros (`TE_SUCCEEDED`, `TE_FAILED`) |
| `te_log.h` | `TE_LogLevel` enum, `TE_LOG_DEBUG` / `TE_LOG_INFO` / `TE_LOG_WARN` / `TE_LOG_ERROR` constants |
| `te_jsonc.h` | `TE_JsoncParse(const char* path)` → `cJSON*`, `TE_JsoncFree(cJSON*)`, `TE_JsoncGetPlugin(cJSON* root, const char* name)` → `cJSON*` |

## Internal Systems Created

| System | Description |
|---|---|
| Build system | CMake with Ninja, MSVC Release + Clang-cl Debug toolchain files |
| Process guard | `IsExplorerProcess()` — compares current PID against Explorer PID |
| Deferred init | `PostMessage(Shell_TrayWnd, WM_APP+100)` from `DllMain`, handled in `InitializeEngine()` |
| JSONC parser | Comment stripping + `cJSON_Parse` wrapper |

## Dependencies on Previous Phases

None — this is the first phase.

## Unit Testing Strategy

- **Catch2** fetched via `FetchContent` in CMake.
- `test_jsonc.cpp`: Verify JSONC comment stripping (single-line `//` comments), valid JSON parsing, nested object access, type coercion, error handling for malformed JSON.
- All tests run via `ctest` from the build directory.
- C code tested from C++ test files via `extern "C"`.

## Performance Considerations

- `DllMain` must complete in < 1 ms (it only stores HINSTANCE and posts a message).
- `InitializeEngine()` must complete in < 10 ms (finds Shell_TrayWnd, logs, returns).
- cJSON parsing of the default config must complete in < 1 ms.
- The Engine DLL's static footprint in non-Explorer processes must be negligible (early exit in `DllMain` prevents any initialization).

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| `SetWindowsHookEx(WH_CBT)` is blocked by antivirus | High — cannot inject | Test with Windows Defender; document AV exclusion. Consider code signing later. |
| `PostMessage` to `Shell_TrayWnd` is not processed | High — no init | Verify Shell_TrayWnd exists before posting. Fallback: use `CreateTimerQueueTimer` for deferred init. |
| Explorer PPL (Protected Process Light) blocks injection | Critical — architecture invalid | Verify PPL status on target builds. As of 22H2-24H2, Explorer is not PPL. |
| DLL loads into many processes via WH_CBT | Low — performance waste | Process guard exits immediately. `DisableThreadLibraryCalls` eliminates thread attach overhead. |

## Deliverables

- [ ] CMake project compiles `TaskbarEngine.exe` + `EngineDLL.dll` + SDK static lib
- [ ] Dual-toolchain support (MSVC + Clang-cl) verified
- [ ] `te_plugin.h` with all ABI types defined
- [ ] cJSON vendored and JSONC parser working with unit tests
- [ ] DLL injection into Explorer verified on Windows 11 22H2+
- [ ] Deferred init via PostMessage verified
- [ ] Clean unload on Tray App exit verified
- [ ] Default `config.jsonc` file created

## Exit Criteria (Definition of Done)

> **Phase 1 is complete when:**
> 1. `cmake --build` succeeds on both MSVC and Clang-cl toolchains.
> 2. `ctest` passes all JSONC parser tests.
> 3. Running `TaskbarEngine.exe` on Windows 11 22H2+ causes `EngineDLL.dll` to load into `explorer.exe`, log "Engine initialized" via `OutputDebugStringW`, and find `Shell_TrayWnd`.
> 4. Closing `TaskbarEngine.exe` cleanly unloads the DLL from Explorer (verified via Process Explorer or similar).
> 5. All ABI types in `te_plugin.h` compile without warnings under `/W4` on both toolchains.
