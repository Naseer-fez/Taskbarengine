# TaskbarEngine — Design Decisions Summary

> Complete record of all architectural decisions made during the design interview.
> This document supersedes `00_Project_Overview.md` wherever there are conflicts.

---

## Project Identity

| Item | Decision |
|---|---|
| **Project name** | TaskbarEngine (working title, to be changed later) |
| **License** | MIT |
| **Repository** | GitHub |
| **Distribution** | Portable ZIP (no installer) |
| **Min Windows version** | Windows 11 22H2 (build 22621)+ |

---

## Process Architecture

| Item | Decision |
|---|---|
| **Model** | Two-process: **Tray App** (EXE) + **Engine DLL** (in Explorer) |
| **Tray App** | Plain Win32 EXE. Owns the `SetWindowsHookEx` handle, system tray icon, and hosts the WinUI 3 settings window in-process via Windows App SDK `DesktopWindow`. |
| **Engine DLL** | Injected into Explorer. Contains Core Manager + SDK. Loads plugin DLLs at runtime. |
| **GUI** | WinUI 3 `DesktopWindow` opened on demand inside the Tray App process. Auto-generated from plugin metadata (pure auto-generation, no layout hints). |
| **Plugin DLLs** | Separate DLLs in `Modules/` directory, loaded by Engine via `LoadLibrary`. |

---

## Injection & Initialization

| Item | Decision |
|---|---|
| **Hook mechanism** | In-process DLL injection into Explorer via `SetWindowsHookEx` |
| **Hook type** | `WH_CBT` (Computer-Based Training hook) |
| **Process guard** | Early exit in non-Explorer processes: `if (GetCurrentProcessId() != explorerPid) return CallNextHookEx(...)` |
| **Initialization** | **Deferred via `PostMessage`**. `DllMain` only: guard check, `DisableThreadLibraryCalls`, store `HINSTANCE`, `PostMessage(Shell_TrayWnd, WM_APP+100)`. Real `InitializeEngine()` runs when Explorer's message pump processes the message — fully outside the loader lock. |
| **Auto-start** | Task Scheduler (`schtasks` at user logon) |

---

## IPC (Tray App ↔ Engine DLL)

| Item | Decision |
|---|---|
| **Transport** | Named Pipes (full duplex, restricted ACL to user SID) |
| **Message format** | Custom binary protocol: `{ uint32 magic; uint32 version; uint32 type; uint32 payload_length; }` + payload bytes |
| **Config delivery** | GUI edits `config.jsonc` directly on disk. Engine detects changes via `ReadDirectoryChangesW`. Pipe used only for non-config commands (plugin state, enable/disable, status). |
| **Write ownership** | GUI always writes config, Engine only reads. No file locking needed. |

---

## Plugin System

| Item | Decision |
|---|---|
| **ABI** | **Pure C function-pointer table (vtable struct)**. Each DLL exports `GetPluginInterface()` returning a `PluginInterface*`. No COM, no C++ at the ABI boundary. |
| **Discovery** | Directory scan of `Modules/*.dll` at startup. `LoadLibrary` + `GetProcAddress("GetPluginInterface")` + `GetMetadata()` for each. |
| **Enable/Disable** | Controlled by `"enabled": true/false` in `config.jsonc` under each `plugin.<name>` section. |
| **Load order** | Priority field (`uint32_t priority`) in `PluginMetadata`. Lower = loaded first. Convention: 0-99 geometry, 100-199 visual, 200-299 behavior, 300+ widgets. |
| **Fault isolation** | SEH (`__try/__except`) around all plugin callbacks + watchdog timer (100ms timeout, N consecutive timeouts → disable plugin). |
| **Disable contract** | `Disable()` must **fully revert** all visual/behavioral changes. Hard contract. |
| **Security** | No signature verification — trust the user. |
| **Inter-plugin communication** | Soft dependencies via typed state queries. Core provides `PublishState` / `QueryState` with a typed variant store (`StateValue` union of `int`, `float`, `bool`, `RECT`). |

### Plugin Interface (C vtable)

```c
typedef struct {
    HRESULT (*Initialize)(const PluginContext*);
    HRESULT (*Enable)(void);
    HRESULT (*Disable)(void);
    HRESULT (*Update)(float deltaTime);
    HRESULT (*Shutdown)(void);
    const PluginMetadata* (*GetMetadata)(void);
    const PluginSettings* (*GetSettings)(void);
} PluginInterface;
```

### Plugin Context (Core → Plugin API)

```c
typedef struct {
    uint32_t api_version;
    HWND taskbar_hwnd;
    HMONITOR monitor;
    uint32_t dpi;
    const cJSON* config;             /* Plugin's config sub-object */
    LogFunc log;
    SubscribeFunc subscribe;
    UnsubscribeFunc unsubscribe;
    RequestRedrawFunc request_redraw;
    PublishStateFunc publish_state;
    QueryStateFunc query_state;
    void* core_opaque;
} PluginContext;
```

### Settings Schema (for GUI auto-generation)

```c
typedef enum {
    SETTING_BOOL, SETTING_INT, SETTING_FLOAT,
    SETTING_STRING, SETTING_ENUM, SETTING_COLOR
} SettingType;

typedef struct {
    const char* key;
    const char* label;
    const char* tooltip;
    SettingType type;
    union {
        struct { bool default_val; } b;
        struct { int default_val, min, max, step; } i;
        struct { float default_val, min, max, step; } f;
        struct { const char* default_val; } s;
        struct { const char* default_val; const char** options; int count; } e;
        struct { uint32_t default_val; } color;
    } value;
} SettingDescriptor;
```

---

## Configuration

| Item | Decision |
|---|---|
| **Format** | **JSONC** (JSON with `//` comments). Parsed by `cJSON` (pure C, ~1000 LOC, single file) + a ~30-line comment stripper. |
| **File** | `config.jsonc` |
| **Location** | `%LOCALAPPDATA%\TaskbarEngine\config.jsonc` |
| **Layout** | Single file with `"plugin": { "<name>": { ... } }` sub-objects |
| **Hot-reload** | `ReadDirectoryChangesW` on config directory, 100ms debounce, diff + dispatch to affected plugins |
| **Version migration** | Backward-compatible defaults + explicit migration functions only for breaking changes. `"version": 1` field in config. |
| **Write ownership** | GUI writes, Engine reads only. |

### Config Example

```jsonc
{
    "version": 1,

    // Core engine settings
    "core": {
        "log_level": "info",
        "log_to_file": false
    },

    // Plugin configurations
    "plugin": {
        "taskbar_resize": {
            "enabled": true,
            "height": 36,
            "padding": 4,
            "margins": 0,
            "icon_spacing": 8
        },
        "icon_hover": {
            "enabled": true,
            "scale": 1.30,
            "radius": 120,
            "curve": "gaussian",
            "speed_ms": 150
        }
    }
}
```

---

## Taskbar Interaction

| Item | Decision |
|---|---|
| **Element discovery** | UI Automation (UIA) — `IUIAutomation` for enumerating taskbar buttons, bounding rects, app IDs |
| **Geometry mutation** | Subclass `Shell_TrayWnd` via `SetWindowSubclass`. Intercept `WM_WINDOWPOSCHANGING` to enforce custom height. `SetWindowPos` for initial resize. `SPI_SETWORKAREA` for desktop work area update. |
| **Icon visual mutation** | DirectComposition overlay — owned child window of `Shell_TrayWnd` with `WS_EX_LAYERED | WS_EX_TRANSPARENT` |
| **Icon bitmap source** | `IImageList` from `SHGetImageList(SHIL_JUMBO/SHIL_EXTRALARGE)` + `IExtractIcon` fallback. Cached in texture atlas. |
| **Subclassing API** | `SetWindowSubclass` (from `comctl32.dll`) — safe multi-subclass support, context pointer via `dwRefData` |

---

## Animation System (Icon Hover)

| Item | Decision |
|---|---|
| **Rendering** | Manual frame loop with `IDCompositionDevice::Commit()`. Set `IDCompositionVisual::SetTransform()` per icon per frame. GPU composites. |
| **Frame timing** | `WM_MOUSEMOVE` + vsync-locked frame timer (via `DwmFlush` or `IDXGIOutput::WaitForVBlank`). Timer runs only while mouse is in taskbar region. Stops on mouse leave + settle completion. |
| **Overlay visibility** | Fully transparent when no hover (zero GPU cost). Fades in on hover enter, fades out on hover leave. |
| **Magnification curves** | Configurable: Gaussian (default), Cosine, Linear, Cubic. Exposed as `"curve"` setting. |
| **DPI** | Per-monitor DPI v2 (`DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`). SDK provides `TE_ScaleDPI()` helper. |
| **Multi-monitor** | Single plugin instance managing all monitors. Events tagged with monitor ID. Per-monitor state array internally. |

---

## Event System

| Item | Decision |
|---|---|
| **Dispatch model** | Synchronous callback dispatch (function pointer invocation). Core maintains `{event_type, plugin_callback}` table. |
| **Event sources (all 7)** | 1. Subclass `Shell_TrayWnd` 2. Shell hook (`RegisterShellHookWindow`) 3. `RegisterPowerSettingNotification` 4. `RegisterDeviceNotification` 5. `ReadDirectoryChangesW` (config) 6. Named pipe listener 7. `IVirtualDesktopNotification` (COM) |

---

## Logging

| Item | Decision |
|---|---|
| **Implementation** | Custom lock-free ring buffer (pre-allocated, fixed-size) + background flush thread. `InterlockedCompareExchange` for atomic writes. Drop oldest on overflow. |
| **Levels** | Debug, Info, Warning, Error |
| **Release default** | Disabled. Enabled via `"log_to_file": true` in config. |
| **Log location** | `%LOCALAPPDATA%\TaskbarEngine\logs\taskbarengine_YYYY-MM-DD.log` |
| **Rotation** | Keep last 5 files, max 5 MB each. |

---

## Error Handling & Resilience

| Item | Decision |
|---|---|
| **Plugin faults** | SEH catch → disable plugin → revert changes → tray balloon notification |
| **Error reporting** | Graceful disable + tray balloon notification: "Plugin 'X' was disabled due to an error. See log for details." |
| **Explorer crash recovery** | `WaitForSingleObject` on Explorer PID (background thread) for death detection + `TaskbarCreated` window message for re-injection timing |
| **Windows update resilience** | Graceful shutdown on `WM_ENDSESSION`. Re-inject after reboot via Task Scheduler. |
| **Shutdown sequence** | Tray App sends `SHUTDOWN` via pipe → Engine calls `Disable()` + `Shutdown()` on each plugin (reverse priority) → Engine sends `SHUTDOWN_COMPLETE` → Tray App calls `UnhookWindowsHookEx` → DLL unloads → Tray App exits |

---

## Language & Build

| Item | Decision |
|---|---|
| **C standard** | C17 (`/std:c17`) |
| **C++ standard** | C++17 (`/std:c++17`) |
| **C usage** | Default language. Config parsing (`cJSON`), plugin loader, logging, utilities, threading, memory management, plugin code. |
| **C++ usage** | Only where C is insufficient: DirectComposition, UI Automation, COM (`CoInitializeEx`, `ComPtr<T>`), WinUI 3 GUI (C++/WinRT). |
| **Rust usage** | Gated: allowed only when benchmarks show >5% improvement in CPU, memory, startup, binary size, or significant safety benefit. |
| **Build system** | CMake + Ninja |
| **Toolchains** | Dual: MSVC for Release, Clang-cl for Debug/Sanitizer builds (ASan, UBSan) |
| **Testing** | Catch2 (C++ header-only). C code tested via `extern "C"` includes. |
| **Benchmarking** | Custom harness for system metrics (CPU %, RSS, startup, FPS) + Google Benchmark for micro-benchmarks of hot-path functions |
| **API docs** | Doxygen for API reference + hand-written Markdown for architecture docs |
| **CI/CD** | Azure DevOps Pipelines |

---

## Binary Layout

| Binary | Contents | Size Target |
|---|---|---|
| `TaskbarEngine.exe` | Tray App (hook owner, tray icon, WinUI 3 GUI host) | ~500 KB–2 MB (includes WinUI 3) |
| `EngineDLL.dll` | Core Manager + SDK + `cJSON` | ~100–200 KB |
| `Modules/taskbar_resize.dll` | TaskbarResize plugin | ~20–50 KB |
| `Modules/icon_hover.dll` | IconHover plugin | ~30–60 KB |

---

## Tray Icon

| Item | Decision |
|---|---|
| **Interaction** | Right-click context menu only. Double-click opens Settings. |
| **Menu items** | Settings, Enable/Disable All, Reload Config, About, Exit |

---

## GUI

| Item | Decision |
|---|---|
| **Framework** | WinUI 3 / Windows App SDK, hosted in Tray App process |
| **Generation** | Pure auto-generated from plugin `GetSettings()` metadata. Type → control mapping: `bool` → ToggleSwitch, `int` → Slider/NumberBox, `float` → Slider, `string` → TextBox, `enum` → ComboBox, `color` → ColorPicker. |
| **No hardcoded pages** | Adding a plugin automatically creates its settings UI. |

---

## Performance Targets

| Metric | Target |
|---|---|
| Idle CPU | 0% |
| Average CPU | < 0.5% |
| Peak CPU | < 2% |
| Idle RAM | < 10 MB |
| Plugin load | < 5 ms |
| Startup | < 50 ms |
| Animation latency | < 2 ms |
| Taskbar redraw | < 1 ms |
| Frame rate | ≥ 60 FPS, target 120 FPS |
| GPU | Negligible except during animations |

---

## Third-Party Dependencies

| Dependency | Type | Purpose | Size |
|---|---|---|---|
| `cJSON` | Vendored C source (single file) | JSONC config parsing | ~1000 LOC |
| `Catch2` | Vendored / fetched via CMake | Unit testing (dev only) | Header-only |
| `Google Benchmark` | Vendored / fetched via CMake | Micro-benchmarks (dev only) | Library |
| Windows App SDK | NuGet / system | WinUI 3 GUI | Runtime |

---

## Decisions That Override `00_Project_Overview.md`

| Topic | Old (Overview) | New (This Interview) |
|---|---|---|
| Plugin ABI | COM-style `IUnknown`-derived | Pure C vtable struct |
| Config format | TOML | JSONC |
| Config parser | Custom minimal TOML in C | `cJSON` (vendored, pure C) |
| Process model | Three-process (Launcher + Engine + GUI) | Two-process (Tray App + Engine DLL) |
| GUI hosting | Separate WinUI 3 EXE | WinUI 3 in-process in Tray App |
| Plugin security | Mandatory signature verification | No verification — trust the user |
| CI/CD | _(not specified)_ | Azure DevOps Pipelines |
