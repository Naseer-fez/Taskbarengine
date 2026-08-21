# 16 — Build System & Toolchain Guide

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: Build System (`CMakeLists.txt`, `CMakePresets.json`)

---

## 1. Build System Overview

TaskbarEngine uses **CMake 3.25+** with the **Ninja** build generator and supports a dual toolchain strategy:
- **MSVC Toolchain**: Highly optimized Release builds with Link-Time Code Generation (`/GL`, `/LTCG`), full `/W4` warning checks treated as errors (`/WX`), and WinUI 3 C++/WinRT integration.
- **Clang-cl Toolchain**: Debug builds equipped with AddressSanitizer (`-fsanitize=address`) to catch memory corruption and leaks during development and CI.

---

## 2. CMake Presets (`CMakePresets.json`)

| Preset Name | Compiler | Build Type | Flags / Sanitizers | Purpose |
|---|---|---|---|---|
| `msvc-release` | MSVC 2022 | `Release` | `/O2 /W4 /WX /GL` | Production binaries & distribution packages |
| `msvc-debug` | MSVC 2022 | `Debug` | `/Zi /Od /W4` | Local Visual Studio debugging |
| `clang-debug` | Clang-cl | `Debug` | `-fsanitize=address -Wall -Werror` | ASan validation & CI memory checking |

---

## 3. Building the Project

### A. Configuring and Building with CMake Presets
```powershell
# Configure MSVC Release build
cmake --preset msvc-release

# Build all targets in parallel
cmake --build build_msvc --config Release

# Build specific target (e.g. Settings GUI)
cmake --build build_msvc --target TaskbarEngineSettings
```

### B. Configuring and Building with Clang-cl + ASan
```powershell
# Configure Clang-cl ASan build
cmake --preset clang-debug

# Build ASan targets
cmake --build build_asan --config Debug
```

---

## 4. Dependencies & Third-Party Management

1. **cJSON**: Vendored directly in `ThirdParty/cJSON/` (single C file `cJSON.c` compiled directly into `te_sdk.lib`).
2. **Catch2**: Automatically fetched at configure time via `FetchContent` (tag `v3.7.1`).
3. **Google Benchmark**: Automatically fetched at configure time via `FetchContent` (tag `v1.9.0`).
4. **Windows App SDK**: Configured via CMake cache variables (`WINAPPSDK_ROOT`, `WINAPPSDK_CPPWINRT`, `WINAPPSDK_FRAMEWORK_DIR`) with side-by-side SxS manifest merging via `mt.exe`.
