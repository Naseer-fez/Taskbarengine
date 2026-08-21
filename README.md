# TaskbarEngine

[![Build Status](https://dev.azure.com/TaskbarEngine/TaskbarEngine/_apis/build/status/TaskbarEngine-CI?branchName=main)](https://dev.azure.com/TaskbarEngine/TaskbarEngine)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2011%20(22H2--24H2)-lightgrey.svg)](Docs/00_Project_Overview.md)

**TaskbarEngine** is a lightweight, high-performance C17/C++17 engine engineered to customize and extend the Windows 11 Taskbar. It utilizes DirectComposition hardware overlays for zero-latency rendering and executes directly within the `explorer.exe` process space via targeted `WH_CBT` hook injection.

---

## Key Highlights

- **Near-Zero Overhead:** 0% CPU at idle, < 0.5% average CPU during active animations, < 10 MB RAM.
- **Hardware-Accelerated Visuals:** DirectComposition overlays with sub-millisecond commit latency and smooth 60+ FPS animations.
- **Pure C Plugin Architecture:** Strict C17 ABI (`te_plugin.h`) with SEH crash containment and dynamic vtable lifecycle.
- **Dynamic Configuration:** JSONC configuration (`config.jsonc`) with comments, directory change watchers, and atomic hot-reloading without restarting Explorer.
- **WinUI 3 Settings GUI:** Auto-generated settings interface mapped dynamically from plugin metadata.
- **Seamless Recovery:** Automatic re-hook and state restoration across Explorer restarts and shell crashes.

---

## Included Plugins

| Plugin | Version | Description | Key Settings |
|---|---|---|---|
| **`taskbar_resize`** | `0.3.0` | Dynamically adjusts taskbar height, padding, and work area margins (`SPI_SETWORKAREA`). | `height` (24–72px), `padding`, `margins`, `icon_spacing` |
| **`icon_hover`** | `0.4.0` | Fluid macOS-style magnification wave over taskbar icons using DirectComposition visual transforms. | `scale` (1.0–2.0x), `radius` (40–300px), `curve` (gaussian/cubic/linear/cosine), `speed_ms` |

---

## System Requirements

- **Operating System:** Windows 11 64-bit (Version 22H2, 23H2, or 24H2; Builds 22621 through 26100).
- **Architecture:** `x86_64` (AMD64 / Intel 64).
- **Permissions:** Standard user rights (No administrative elevation required for normal operation).

---

## Quick Start & Installation

1. **Download:** Download the latest `TaskbarEngine-v1.0.0.zip` from the releases page.
2. **Extract:** Extract the ZIP package to any directory on your system (e.g., `D:\Programs\TaskbarEngine`).
3. **Run:** Launch `TaskbarEngine.exe`.
   - The engine will seamlessly inject into Explorer and place an icon in your system notification area (System Tray).
   - An auto-start scheduled task (`TaskbarEngine_Logon`) will be registered to start the utility at user logon.

---

## Configuration & Usage

### 1. WinUI 3 Settings GUI
- **Open Settings:** Double-click the TaskbarEngine tray icon or right-click and choose **Settings**.
- Modify plugin toggles, sliders, and drop-downs. Changes take effect immediately in real-time.

### 2. Manual JSONC Configuration
Configuration is stored in `%LOCALAPPDATA%\TaskbarEngine\config.jsonc`:

```jsonc
{
  "version": 1,
  "core": {
    "log_level": "info",
    "log_to_file": true
  },
  "plugin": {
    "taskbar_resize": {
      "enabled": true,
      "height": 48,
      "padding": 4,
      "margins": 0,
      "icon_spacing": 8
    },
    "icon_hover": {
      "enabled": true,
      "scale": 1.35,
      "radius": 130,
      "curve": "gaussian",
      "speed_ms": 150
    }
  }
}
```

---

## Building from Source

### Prerequisites
- **Visual Studio 2022** (v17.4+ with *Desktop development with C++*)
- **CMake** 3.25+
- **Ninja** build system
- **Windows 11 SDK** (10.0.22621.0 or newer)
- **Windows App SDK** (1.5+ for WinUI 3 GUI)

### Build Commands

```powershell
# Configure with MSVC Release Preset
cmake -B build_msvc -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build all binaries (Engine, App, Plugins, Tests, Benchmarks)
cmake --build build_msvc --config Release

# Run Catch2 Unit Test Suite
cd build_msvc
ctest -C Release --output-on-failure

# Run Google Micro-Benchmark Suite
.\bin\te_benchmarks.exe

# Package release ZIP
powershell -File ..\Scripts\package.ps1 -BuildDir build_msvc -DestinationZip "..\TaskbarEngine-v1.0.0.zip"
```

---

## Uninstallation

To cleanly remove TaskbarEngine:
1. Run `Scripts\uninstall.ps1` from the extracted directory or execute:
   ```powershell
   TaskbarEngine.exe --uninstall
   ```
2. Delete the application directory.

---

## Documentation

Full architectural specifications, design decisions, and component references are located in the `Docs/` directory:

- [00_Project_Overview.md](Docs/00_Project_Overview.md) — High-level architecture and principles
- [01_System_Architecture.md](Docs/01_System_Architecture.md) — Two-process model and hook injection
- [04_Plugin_System.md](Docs/04_Plugin_System.md) — Pure C ABI and lifecycle contracts
- [06_GUI.md](Docs/06_GUI.md) — WinUI 3 dynamic settings generation
- [09_Rendering.md](Docs/09_Rendering.md) — DirectComposition overlay engine
- [12_Performance.md](Docs/12_Performance.md) — Performance guarantees and measurement methodology

---

## License

TaskbarEngine is licensed under the [MIT License](LICENSE).
