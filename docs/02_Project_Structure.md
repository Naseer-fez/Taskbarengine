# 02 — Project Structure

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: Build System & Source Tree Organization

---

## 1. Directory Tree & Structural Layout

The TaskbarEngine repository is organized as a modular CMake workspace separating shared SDK contracts, in-process engine logic, out-of-process helper tools, isolated plugins, test suites, and documentation:

```
TaskbarEngine/
├── CMakeLists.txt              # Top-level workspace CMake definition
├── CMakePresets.json           # Dual toolchain presets (MSVC Release + Clang-cl Debug)
├── Doxyfile                    # Doxygen HTML API documentation generator configuration
├── azure-pipelines.yml         # Azure DevOps multi-stage CI/CD workflow
├── README.md                   # User-facing summary and quickstart guide
├── LICENSE                     # MIT License
├── SDK/                        # Shared static library & C17 ABI headers
│   ├── CMakeLists.txt
│   ├── include/sdk/
│   │   ├── te_types.h          # Core types, error codes, and macro definitions
│   │   ├── te_plugin.h         # Pure C Plugin ABI vtable & context interfaces
│   │   ├── te_ipc.h            # Named pipe protocol constants and serializing utilities
│   │   ├── te_events.h         # Engine event IDs and event payload structures
│   │   ├── te_log.h            # Logging API interfaces
│   │   ├── te_jsonc.h          # JSONC parsing wrapper
│   │   ├── te_version.h        # OS build detection and compatibility validation
│   │   ├── te_easing.h         # Mathematical easing functions
│   │   └── te_dpi.h            # Per-monitor DPI scaling helpers
│   └── src/                    # SDK implementation sources
├── Core/                       # Injected Engine DLL (in-process Explorer host)
│   ├── CMakeLists.txt
│   ├── include/core/           # Core Manager, Subclassing, IPC server, Hook Manager
│   └── src/                    # Core lifecycle, fault isolation, state store, config watcher
├── App/                        # Out-of-process Tray Host & WinUI 3 Settings GUI
│   ├── CMakeLists.txt
│   ├── include/app/            # Tray menu, crash recovery, and scheduler headers
│   ├── src/                    # Tray icon, WinUI 3 XAML-less GUI, Task Scheduler COM
│   └── res/                    # Manifests, icons, and resource definitions
├── Modules/                    # Independent feature plugin DLLs
│   ├── taskbar_resize/         # Taskbar geometry & SPI_SETWORKAREA resize plugin
│   ├── icon_hover/             # DirectComposition icon magnification wave plugin
│   ├── dummy/                  # Test mock plugin exposing all setting types
│   └── fault/                  # Fault-injection crash-testing plugin
├── Tests/                      # Catch2 unit and integration test suite
│   ├── CMakeLists.txt
│   └── test_*.cpp              # Module unit tests
├── Benchmarks/                 # Google Benchmark micro-benchmarks & System test harness
│   ├── CMakeLists.txt
│   ├── bench_system.cpp        # Real system CPU/RAM/FPS benchmark harness
│   └── bench_*.cpp             # Google Benchmark micro-benchmark modules
├── Config/                     # Default configuration templates & schema
│   ├── default_config.jsonc    # Base configuration file with comments
│   └── settings_schema.json    # JSON descriptor schema for GUI verification
├── Scripts/                    # Automation and administrative PowerShell scripts
│   ├── package.ps1             # Release build, staging, and ZIP packaging script
│   └── uninstall.ps1           # Scheduled task unregistration and cleanup script
├── Docs/                       # 00-19 Complete technical documentation series
└── ThirdParty/                 # Vendored third-party dependencies
    └── cJSON/                  # Single-file ANSI C JSON parser
```

---

## 2. CMake Targets & Build Matrix

| CMake Target | Target Type | Language | Output Path | Description |
|---|---|---|---|---|
| `te_sdk` | Static Library | C17 | `lib/te_sdk.lib` | Shared SDK runtime, cJSON parser, IPC serializer, and math helpers. |
| `EngineDLL` | Shared Library (DLL) | C17 / C++17 | `bin/EngineDLL.dll` | The in-process engine injected into `explorer.exe`. |
| `TaskbarEngine` | Executable (Win32) | C17 / C++17 | `bin/TaskbarEngine.exe` | Notification tray host and injection manager. |
| `TaskbarEngineSettings` | Executable (WinUI 3) | C++17 / WinRT | `bin/TaskbarEngineSettings.exe` | Auto-generated settings UI. |
| `taskbar_resize` | Shared Library (DLL) | C17 | `bin/Modules/taskbar_resize.dll` | Taskbar dimension customization plugin. |
| `icon_hover` | Shared Library (DLL) | C17 / C++17 | `bin/Modules/icon_hover.dll` | DirectComposition magnification plugin. |
| `te_tests` | Executable (Catch2) | C++17 | `bin/te_tests.exe` | Complete unit test runner. |
| `te_benchmarks` | Executable (GoogleBench)| C++17 | `bin/te_benchmarks.exe` | In-process micro-benchmark suite. |
| `bench_system` | Executable | C17 / C++17 | `bin/bench_system.exe` | System-level Explorer telemetry harness. |

---

## 3. Header Inclusion Hierarchy & Conventions

1. **Self-Header First**: Every `.c` or `.cpp` file must include its own corresponding header file as the very first line of code.
2. **System Headers**: Windows and standard C/C++ headers follow (e.g., `<windows.h>`, `<stdbool.h>`, `<winrt/...>`).
3. **Project SDK Headers**: Included via `<sdk/...>` (e.g., `<sdk/te_types.h>`, `<sdk/te_plugin.h>`).
4. **Internal Component Headers**: Included via `<core/...>` or local relative paths.
5. **Include Guards**: `#pragma once` is standard across all internal and public headers.
