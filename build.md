# Building & Customizing TaskbarEngine

This guide provides end-to-end instructions for compiling, testing, benchmarking, packaging, and customizing TaskbarEngine from source.

---

## 1. Toolchain Prerequisites & Environment Setup

TaskbarEngine requires native Windows compilation toolchains supporting C17, C++17, and C++/WinRT.

### Required Software & SDKs

| Dependency | Minimum Version | Notes |
|---|---|---|
| **Operating System** | Windows 11 (22H2+) | Build 22621 or higher required for DirectComposition XAML visual interop. |
| **Visual Studio** | 2022 (v17.4+) | Install workload: **Desktop development with C++**. |
| **MSVC Toolset** | v143 (19.34+) | `cl.exe` with support for `/std:c17` and `/std:c++17`. |
| **Windows 11 SDK** | `10.0.22621.0`+ | Included with VS Installer. Provides `dcomp.lib`, `comctl32.lib`, and WinRT headers. |
| **CMake** | 3.25 or higher | Must be discoverable on system `PATH`. |
| **Ninja** | 1.11 or higher | Fast build generator utilized by default presets. |
| **Clang-cl** *(Optional)* | LLVM 16.0+ | Required for AddressSanitizer (`-fsanitize=address`) debug configurations. |

> [!WARNING]
> **Compiler Support Notice:**
> MinGW, GCC, and GNU-style Clang are **not supported**. TaskbarEngine interacts directly with Windows COM interfaces, Structured Exception Handling (`__try`/`__except`), side-by-side WinUI manifests, and Microsoft ABI structures requiring `cl.exe` or `clang-cl.exe`.

### Environment Verification

Launch **x64 Native Tools Command Prompt for VS 2022** (or open PowerShell after initializing developer environment variables):

```powershell
# Verify compilers and tools
cl.exe
cmake --version
ninja --version
```

---

## 2. CMake Presets (`CMakePresets.json`)

TaskbarEngine utilizes standard CMake configuration and build presets:

| Preset Name | Compiler | Build Type | Output Directory | Primary Use Case |
|---|---|---|---|---|
| `msvc-release` | MSVC 2022 (`cl`) | `Release` | `build/msvc-release` | Optimized production builds (`/O2 /GL /LTCG`). |
| `msvc-debug` | MSVC 2022 (`cl`) | `Debug` | `build/msvc-debug` | Interactive debugging with PDB symbols (`/Zi /Od`). |
| `clang-debug` | Clang-cl | `Debug` | `build/clang-debug` | AddressSanitizer (`-fsanitize=address`) memory checks. |
| `clang-release`| Clang-cl | `Release` | `build/clang-release` | Clang-optimized release builds. |

---

## 3. Step-by-Step Compilation Guide

### Step 1: Clone the Repository

```powershell
git clone https://github.com/Naseer-fez/Taskbarengine.git
cd Taskbarengine
```

### Step 2: Configure the Build Directory

Execute CMake using the preferred preset:

```powershell
# Production Release Build
cmake --preset msvc-release

# Debug Build for Local Development
cmake --preset msvc-debug
```

### Step 3: Build the Targets

Compile all targets in parallel:

```powershell
# Build entire suite (Host app, Engine DLL, Plugins, Tests, Benchmarks)
cmake --build --preset msvc-release

# Or build a specific target:
cmake --build build/msvc-release --target EngineDLL
cmake --build build/msvc-release --target TaskbarEngine
cmake --build build/msvc-release --target icon_hover
```

### Step 4: Run Unit Tests

Unit tests are built using Catch2 (v3.7.1, automatically resolved via `FetchContent`):

```powershell
# Run tests using CTest
ctest --test-dir build/msvc-release --output-on-failure

# Or invoke the test binary directly:
.\build\msvc-release\bin\te_tests.exe
```

### Step 5: Run Micro-Benchmarks

Google Benchmark (v1.9.0) validates real-time performance thresholds:

```powershell
.\build\msvc-release\bin\te_benchmarks.exe
```

---

## 4. Release Packaging & Deployment

To create an isolated distribution directory matching the production layout:

```powershell
# Run the automated packaging script
powershell -NoProfile -ExecutionPolicy Bypass -File .\Scripts\package.ps1 -BuildDir build/msvc-release -DestinationZip "TaskbarEngine-v1.0.0.zip"
```

### Binary Layout Structure

```
TaskbarEngine/
├── TaskbarEngine.exe              # Out-of-process tray host & injector
├── EngineDLL.dll                  # In-process engine injected into Explorer
├── TaskbarEngineSettings.exe      # WinUI 3 settings application
├── te_sdk.lib                     # Static SDK library
├── Config/
│   ├── default_config.jsonc       # Default configuration template
│   └── start_button.png           # Custom start button asset
└── Modules/
    ├── taskbar_resize.dll         # Taskbar height & work area plugin
    ├── icon_hover.dll             # DirectComposition icon magnification plugin
    └── DockPhysics.dll            # Spring-physics dock animation plugin
```

---

## 5. Customizing TaskbarEngine for Your System

TaskbarEngine offers two customization avenues: modifying configuration parameters at runtime, or authoring custom plugins.

### Option A: Customizing Configuration (`config.jsonc`)

Runtime configuration is located at `%LOCALAPPDATA%\TaskbarEngine\config.jsonc`. Edits are detected automatically via directory change notifications and reloaded without restarting Windows Explorer.

#### Customizing Taskbar Dimensions (`taskbar_resize`)

Adjust taskbar thickness, padding, and icon density:

```jsonc
"taskbar_resize": {
  "enabled": true,
  "height": 48,          // Custom taskbar height in pixels (default: 48, compact: 36, tall: 64)
  "padding_top": 4,      // Top clearance in pixels
  "padding_bottom": 4,   // Bottom clearance in pixels
  "icon_spacing": 16     // Distance between pinned and active app icons
}
```

#### Customizing Visual Physics (`icon_hover`)

Adjust macOS-style magnification wave dynamics:

```jsonc
"icon_hover": {
  "enabled": true,
  "max_scale": 1.75,          // Maximum icon scale factor (1.0 - 2.5)
  "radius": 160,              // Influence radius in pixels across adjacent icons
  "curve": "gaussian",        // Wave falloff: "gaussian" | "cubic" | "linear" | "cosine"
  "speed_ms": 180,            // Transition duration in milliseconds
  "bounce_enabled": true,     // Spring rebound effect on mouse exit
  "bounce_strength": 400,     // Damping spring stiffness
  "tilt_enabled": true,       // 3D perspective tilt towards mouse cursor
  "max_tilt_angle": 15,       // Maximum rotation angle in degrees
  "start_image_path": "Config/custom_start.png" // Path to custom Start Menu button graphic
}
```

---

### Option B: Developing a Custom Plugin

TaskbarEngine plugins are lightweight dynamic link libraries implementing the C17 ABI defined in `SDK/include/sdk/te_plugin.h`.

#### 1. Implement the Lifecycle Contract

Create `my_plugin.c`:

```c
#include <sdk/te_plugin.h>
#include <windows.h>

static const PluginContext* g_ctx = NULL;

static HRESULT MyPlugin_Initialize(const PluginContext* ctx) {
    g_ctx = ctx;
    g_ctx->log(TE_LOG_INFO, "MyPlugin", "Plugin initialized successfully.");
    return S_OK;
}

static HRESULT MyPlugin_Enable(void) {
    g_ctx->log(TE_LOG_INFO, "MyPlugin", "Plugin enabled.");
    // Install custom window message hooks or DirectComposition visual transforms
    return S_OK;
}

static HRESULT MyPlugin_Disable(void) {
    g_ctx->log(TE_LOG_INFO, "MyPlugin", "Plugin disabled. Reverting visual state.");
    // Revert visual state to native geometry
    return S_OK;
}

static HRESULT MyPlugin_Update(float delta_time) {
    // Per-frame physics or animation step
    return S_OK;
}

static HRESULT MyPlugin_Shutdown(void) {
    g_ctx = NULL;
    return S_OK;
}

static const PluginMetadata g_metadata = {
    .name = "my_custom_plugin",
    .version = "1.0.0",
    .author = "Developer",
    .description = "Custom TaskbarEngine visual extension",
    .priority = 50,
    .min_os_build = 22621,
    .max_os_build = 0
};

static const PluginMetadata* MyPlugin_GetMetadata(void) {
    return &g_metadata;
}

static const PluginSettings* MyPlugin_GetSettings(void) {
    return NULL; // Return setting descriptors for WinUI 3 auto-generation
}

static const PluginInterface g_interface = {
    .Initialize  = MyPlugin_Initialize,
    .Enable      = MyPlugin_Enable,
    .Disable     = MyPlugin_Disable,
    .Update      = MyPlugin_Update,
    .Shutdown    = MyPlugin_Shutdown,
    .GetMetadata = MyPlugin_GetMetadata,
    .GetSettings = MyPlugin_GetSettings
};

TE_EXPORT const PluginInterface* GetPluginInterface(void) {
    return &g_interface;
}
```

#### 2. Register in CMake

Add the plugin to `Modules/CMakeLists.txt`:

```cmake
add_library(my_custom_plugin SHARED my_plugin.c)
target_link_libraries(my_custom_plugin PRIVATE te_sdk)
set_target_properties(my_custom_plugin PROPERTIES
    OUTPUT_NAME "my_custom_plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Modules"
)
```

Compile and place the resulting `my_custom_plugin.dll` into the `Modules/` folder. The engine discovers and binds it automatically.

---

## 6. Troubleshooting & Diagnostics

### Issue: Hook injection fails (`Shell_TrayWnd not found`)
- **Cause:** Explorer has crashed or has not finished launching its main window thread.
- **Remedy:** Ensure `explorer.exe` is running under the current user session. Run `TaskbarEngine.exe` without elevation (admin elevation is neither required nor recommended).

### Issue: Configuration changes are not applying
- **Cause:** JSON syntax error in `config.jsonc`.
- **Diagnostics:** Check `%LOCALAPPDATA%\TaskbarEngine\TaskbarEngine.log`. If the JSON parser encounters a syntax violation, it rejects the file and maintains the previously valid configuration in memory.

### Issue: ASan build reports compiler warnings with Windows headers
- **Cause:** AddressSanitizer headers conflict with certain Win32 COM macros when `/WX` is enabled.
- **Remedy:** Use `cmake --preset clang-debug` which applies appropriate warning suppression flags (`/wd5105`, `/wd4459`) defined in the root `CMakeLists.txt`.
