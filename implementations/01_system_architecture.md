# Part 1 — System Architecture

> Technical Reconstruction Specification — TaskbarEngine

---

## 1.1 Project Purpose and Core Responsibilities

TaskbarEngine is a modular, plugin-driven platform for customizing the Windows 11 taskbar. It injects a DLL into `explorer.exe` and uses documented Win32 APIs (subclassing, DirectComposition, UI Automation) to modify taskbar geometry and icon behavior while consuming near-zero resources when idle.

**Core responsibilities:**

1. **Customization** — Apply user-requested visual and behavioral changes to the Windows 11 taskbar.
2. **Stability** — Never crash, hang, or destabilize `explorer.exe`.
3. **Performance** — Meet strict idle/average/peak resource budgets (0% idle CPU, <10 MB RAM).
4. **Modularity** — Load, unload, and route events to independent feature plugins without coupling.
5. **Extensibility** — Allow new features as plugins without modifying the Core Manager.
6. **Configurability** — Parse, validate, hot-reload JSONC configuration.
7. **Observability** — Provide asynchronous, level-filtered logging.

**Priority ranking (when goals conflict, higher wins):**

1. Performance
2. Stability
3. Maintainability
4. Extensibility
5. Minimal Resource Usage

---

## 1.2 Major Subsystems

The system consists of exactly **two processes** and **N plugin DLLs**:

### A. Tray App (`TaskbarEngine.exe`) — Out-of-Process Host

**Language**: C17 (core) + C++17/WinRT (WinUI 3 GUI)

**Responsibilities:**
- Owns the `SetWindowsHookEx(WH_CBT)` handle for DLL injection
- Manages the system tray icon and right-click context menu
- Hosts the WinUI 3 Settings GUI in-process (opened on demand)
- Monitors Explorer process health (crash recovery background thread)
- Sends IPC commands to Engine via Named Pipe client
- Registers Task Scheduler entry for auto-start at logon
- Initiates graceful shutdown sequence

**Binary**: `TaskbarEngine.exe` (~500 KB–2 MB with WinUI 3)

### B. Engine DLL (`EngineDLL.dll`) — In-Process Inside Explorer

**Language**: C17 (core, plugins, config, logging) + C++17 (COM, DComp, UIA)

**Responsibilities:**
- Contains the Core Manager (singleton orchestrator)
- Installs `SetWindowSubclass` on `Shell_TrayWnd`
- Loads plugin DLLs from `Modules/` directory
- Parses and hot-reloads `config.jsonc`
- Dispatches events synchronously to subscribed plugins
- Wraps all plugin calls in SEH + watchdog
- Hosts the Named Pipe IPC server
- Manages the lock-free ring buffer logger
- Hosts the inter-plugin shared state store

**Binary**: `EngineDLL.dll` (~100–200 KB)

### C. Plugin DLLs (`Modules/*.dll`) — Feature Modules

Each plugin is a separate DLL exporting exactly one C function:

```c
TE_EXPORT const PluginInterface* GetPluginInterface(void);
```

**v1 Plugins:**

| Plugin | Binary | Language | Purpose |
|---|---|---|---|
| `taskbar_resize` | `Modules/taskbar_resize.dll` (~20–50 KB) | C17 | Dynamic taskbar height, padding, margins, icon spacing |
| `icon_hover` | `Modules/icon_hover.dll` (~30–60 KB) | C17 + C++17 | macOS Dock-style icon magnification via DirectComposition |

---

## 1.3 Subsystem Responsibilities Detail

### Core Manager (`core_manager.c` / `core_manager.h`)

The central orchestrator inside `EngineDLL.dll`. It:
- Owns the singleton `TE_CoreState` struct (all engine state)
- Coordinates two-phase initialization (Phase A in DllMain, Phase B deferred)
- Loads config, starts watchers, discovers and loads plugins
- Routes window messages and events to plugins
- Manages graceful shutdown (reverse-priority plugin teardown)

### Event Dispatch (`event_dispatch.c`)

- Maintains a subscription table: `{event_type, callback_fn, plugin_id}`
- Synchronous dispatch — direct function pointer invocation
- Supports targeted delivery by plugin ID
- SEH-wrapped per-callback invocation
- Fixed-size table (64 subscriptions max)

### Plugin Loader (`plugin_loader.c`)

- Scans `Modules/*.dll` at startup
- `LoadLibrary` + `GetProcAddress("GetPluginInterface")` + `GetMetadata()` for each
- Maintains a sorted registry: `{PluginInterface*, PluginMetadata*, HMODULE, enabled, fault_count}`
- Priority-sorted: lower number = loaded first
- Convention: 0–99 geometry, 100–199 visual, 200–299 behavior, 300+ widgets

### Fault Isolation (`fault_isolation.c`)

- SEH `__try/__except` around every plugin callback
- Watchdog timer (100 ms timeout per callback via `CreateTimerQueueTimer`)
- N-strike policy: 3 consecutive timeouts → plugin disabled
- On fault: log exception, disable plugin, revert changes, show tray balloon

### Config System (`config.c`, `config_watcher.c`)

- Parses `%LOCALAPPDATA%\TaskbarEngine\config.jsonc` via `cJSON` + comment stripper
- Hot-reload via `ReadDirectoryChangesW` with 100 ms debounce
- Diff-based dispatch: only affected plugins receive `TE_EVENT_CONFIG_CHANGED`
- Atomic swap: old config stays active until new config is fully validated
- GUI writes config, Engine only reads (no file locking needed)

### IPC Server (`ipc_server.c`)

- Named Pipe server: `\\.\pipe\TaskbarEngine`
- Binary protocol: `{uint32 magic, uint32 version, uint32 type, uint32 payload_length}` + payload
- Async overlapped I/O, restricted ACL (current user SID only)
- Commands: `SHUTDOWN`, `GET_PLUGIN_LIST`, `ENABLE_PLUGIN`, `DISABLE_PLUGIN`, `RELOAD_CONFIG`, `GET_SETTINGS`
- **CRITICAL**: All state-mutating commands must be marshaled to the UI thread via `PostMessage` (never mutate state on IPC thread)

### Logging (`te_log_impl.c`)

- Lock-free ring buffer (64 KB, pre-allocated)
- `InterlockedCompareExchange` for atomic writes
- Background flush thread drains to file every 100 ms
- Drop oldest on overflow
- Levels: Debug (compiled out in Release), Info, Warning, Error
- Output: `%LOCALAPPDATA%\TaskbarEngine\logs\taskbarengine_YYYY-MM-DD.log`
- Rotation: keep last 5 files, max 5 MB each

### Shared State Store (`state_store.c`)

- Fixed-capacity hash map (256 entries)
- Key: `"plugin_name.key"` (string)
- Value: `StateValue` (typed union: int, float, bool, RECT)
- `SRWLock`: shared reads, exclusive writes
- Used for inter-plugin soft dependencies (e.g., TaskbarResize publishes height, IconHover queries it)

### Taskbar Subclass (`taskbar_subclass.c`)

- `SetWindowSubclass(Shell_TrayWnd, TE_TaskbarSubclassProc, TE_SUBCLASS_ID, (DWORD_PTR)state)`
- Handles custom messages: `WM_TE_INIT`, `WM_TE_IPC_COMMAND`, `WM_TE_TIMER_TICK`
- Forwards subscribed Win32 messages to plugins via the message filter table
- Always calls `DefSubclassProc` for unhandled messages

---

## 1.4 Important Structures and Interfaces

### TE_CoreState (Singleton — all engine state)

```c
struct TE_CoreState {
    HINSTANCE           hinstance;           /* EngineDLL.dll module handle */
    HWND                taskbar_hwnd;        /* Primary Shell_TrayWnd */
    HMONITOR            primary_monitor;     /* Monitor hosting primary taskbar */
    uint32_t            dpi;                 /* Taskbar DPI scaling */
    cJSON*              config_root;         /* Current parsed JSONC config */
    SRWLOCK             config_lock;         /* Multi-reader single-writer config lock */
    TE_PluginEntry*     plugins;             /* Sorted array of loaded plugins */
    uint32_t            plugin_count;        /* Number of loaded plugins */
    uint32_t            plugin_capacity;     /* Allocated capacity */
    uint32_t            current_plugin_id;   /* Active plugin ID for event attribution */
    TE_ConfigWatcher*   watcher;             /* Directory change notification */
    TE_IpcServer*       ipc_server;          /* Named pipe server instance */
    TE_StateStore*      state_store;         /* Inter-plugin blackboard */
    TE_MsgFilterTable*  msg_filter;          /* Subscribed window message registry */
    TE_TimerQueue*      timer_queue;         /* UI thread timer list */
};
```

### PluginInterface (Pure C VTable — Frozen ABI)

```c
typedef struct PluginInterface {
    HRESULT (*Initialize)(const PluginContext* ctx);
    HRESULT (*Enable)(void);
    HRESULT (*Disable)(void);
    HRESULT (*Update)(float deltaTime);
    HRESULT (*Shutdown)(void);
    const PluginMetadata* (*GetMetadata)(void);
    const PluginSettings* (*GetSettings)(void);
} PluginInterface;
```

### PluginContext (Engine → Plugin API)

```c
typedef struct PluginContext {
    /* ABI envelope */
    uint32_t struct_size;           /* sizeof(PluginContext) */
    uint32_t api_version;           /* TE_API_VERSION */

    /* v1 fields (frozen) */
    HWND taskbar_hwnd;
    HMONITOR monitor;
    uint32_t dpi;
    const cJSON* config;            /* Plugin's config sub-object (read-only) */
    LogFunc log;
    SubscribeFunc subscribe;
    UnsubscribeFunc unsubscribe;
    RequestRedrawFunc request_redraw;
    PublishStateFunc publish_state;
    QueryStateFunc query_state;
    void* core_opaque;

    /* v2 fields (appended, check via TE_CTX_HAS_FIELD) */
    SubscribeToMessageFunc subscribe_message;
    UnsubscribeFromMessageFunc unsubscribe_message;
    RegisterTimerFunc register_timer;
    CancelTimerFunc cancel_timer;
} PluginContext;
```

### SettingDescriptor (GUI Auto-Generation Schema)

```c
typedef enum SettingType {
    TE_SETTING_BOOL, TE_SETTING_INT, TE_SETTING_FLOAT,
    TE_SETTING_STRING, TE_SETTING_ENUM, TE_SETTING_COLOR
} SettingType;

typedef struct SettingDescriptor {
    const char* key;
    const char* label;
    const char* tooltip;
    SettingType type;
    union { /* typed default + constraints per type */ } value;
} SettingDescriptor;
```

### IPC Header (Binary Protocol)

```c
typedef struct TE_IpcHeader {
    uint32_t magic;           /* 0x54454950 ('TEIP') */
    uint32_t version;         /* 1 */
    uint32_t type;            /* TE_IpcMsgType enum */
    uint32_t payload_length;  /* max 64 KB */
} TE_IpcHeader;
```

---

## 1.5 Communication Between Subsystems

| Path | Mechanism | Direction | Purpose |
|---|---|---|---|
| Tray App → Explorer | `SetWindowsHookEx(WH_CBT)` | One-shot | DLL injection |
| Tray App → Engine | Named Pipe (binary) | Bidirectional | Commands (shutdown, enable/disable, reload) |
| GUI → Disk | File I/O (atomic write) | Write | Config persistence |
| GUI → Engine | Named Pipe | Request/Reply | Query plugin metadata and settings schema |
| Engine → Plugins | Function pointer vtable | Synchronous | Lifecycle (Init/Enable/Disable/Shutdown) |
| Engine → Plugins | Event subscription table | Synchronous | Event dispatch (config change, shell hook, etc.) |
| Plugins → Engine | `PluginContext` function pointers | Synchronous | Log, subscribe, publish state, request timer |
| Plugin ↔ Plugin | State Store (blackboard) | Async (SRWLock) | Soft dependencies (publish/query typed values) |
| Config file → Engine | `ReadDirectoryChangesW` | Async notification | Hot-reload trigger |

---

## 1.6 Data Flow

### Configuration Data Flow

```
User edits GUI slider
  → GUI writes config.jsonc atomically (tmp + MoveFileExW)
  → GUI sends TE_IPC_MSG_RELOAD_CONFIG via Named Pipe
  → Engine config_watcher.c detects file change (ReadDirectoryChangesW)
  → 100 ms debounce timer fires
  → TE_JsoncParse() parses new config into temporary cJSON tree
  → Config diff engine compares old vs new per-plugin sections
  → TE_EVENT_CONFIG_CHANGED dispatched to affected plugins only
  → Plugin re-reads config values and updates behavior
  → Old config freed after successful swap
```

### Event Data Flow (Mouse Move Example)

```
Explorer receives WM_MOUSEMOVE on Shell_TrayWnd
  → TE_TaskbarSubclassProc intercepts message
  → Message filter table checks if any plugin subscribed to WM_MOUSEMOVE
  → If subscribed: build TE_TaskbarMouseEvent payload
  → Synchronous dispatch to all subscribers for TE_EVENT_TASKBAR_MOUSE
  → IconHover plugin callback receives event
  → Plugin starts/continues animation frame loop
  → DefSubclassProc forwards message to Explorer
```

---

## 1.7 Object Ownership and Lifetime

| Object | Owner | Lifetime | Cleanup |
|---|---|---|---|
| `TE_CoreState` | Engine (Core Manager) | Phase A init → shutdown | `free()` in `TE_CoreManagerShutdown` |
| `cJSON* config_root` | `TE_CoreState` | Load → next reload or shutdown | `cJSON_Delete()` after swap |
| `HMODULE` (plugin DLLs) | Plugin Loader | `LoadLibrary` → `FreeLibrary` in shutdown | Reverse-order unload |
| `PluginContext` | Engine (stack-allocated per `Initialize` call) | `Initialize` → `Shutdown` | Stack unwinding |
| `IDCompositionDevice` | IconHover plugin | `Enable` → `Disable` | `Release()` via `ComPtr<T>` |
| `IDCompositionVisual` array | IconHover plugin | `Enable` → `Disable` | `Release()` per visual |
| Named Pipe handle | IPC Server / Client | Server start → stop | `CloseHandle()` |
| `Shell_TrayWnd` subclass | Engine | Phase A → shutdown | `RemoveWindowSubclass()` |
| Ring buffer memory | Logger | `TE_LogInit` → `TE_LogShutdown` | `free()` |
| Explorer process handle | Crash Recovery (Tray App) | `OpenProcess` → thread exit | `CloseHandle()` |

---

## 1.8 Startup and Initialization Sequence

```
1. User runs TaskbarEngine.exe (or Task Scheduler launches it at logon)
2. Tray App WinMain:
   a. Create hidden HWND for message pump
   b. Shell_NotifyIcon → system tray icon
   c. FindWindowW(L"Shell_TrayWnd") → get taskbar HWND
   d. GetWindowThreadProcessId → get Explorer thread ID
   e. Resolve absolute path to EngineDLL.dll (via GetModuleFileNameW)
   f. LoadLibraryW(absolute_path) → load DLL into Tray App
   g. SetWindowsHookEx(WH_CBT, hook_proc, dll_handle, explorer_thread_id)
   h. PostMessage(Shell_TrayWnd, WM_NULL) → force hook activation
   i. Start crash recovery thread: WaitForSingleObject(explorer_handle, INFINITE)
   j. Enter message pump loop

3. EngineDLL.dll DllMain (DLL_PROCESS_ATTACH) [in Explorer's address space]:
   a. If not Explorer process → return TRUE immediately (process guard)
   b. DisableThreadLibraryCalls(hinstDLL)
   c. Store HINSTANCE
   d. Allocate TE_CoreState (Phase A)
   e. FindWindowW(L"Shell_TrayWnd") → store taskbar HWND
   f. SetWindowSubclass on Shell_TrayWnd
   g. PostMessage(Shell_TrayWnd, WM_TE_INIT) → defer Phase B
   h. Return TRUE

4. Explorer message pump processes WM_TE_INIT → Phase B:
   a. Parse config.jsonc
   b. Start config watcher thread (ReadDirectoryChangesW)
   c. Initialize event dispatch table, state store, message filter, timer queue
   d. Scan Modules/ directory, load plugin DLLs
   e. Call Initialize() on all plugins (priority order)
   f. Call Enable() on enabled plugins (priority order)
   g. Start IPC server thread (Named Pipe listener)
   h. Log "Engine initialized"
```

---

## 1.9 Runtime Flow

**Idle state**: Zero CPU. No timers firing. No polling. All threads are blocked on kernel wait objects:
- Main thread: `GetMessage` (Explorer's message pump)
- Config watcher: `ReadDirectoryChangesW` (kernel I/O completion)
- IPC server: `ConnectNamedPipe` overlapped (kernel wait)
- Log flush: `WaitForSingleObject` on flush event (kernel wait)

**Active state (mouse over taskbar)**:
- `WM_MOUSEMOVE` arrives at subclass proc
- Plugin subscribing to `TE_EVENT_TASKBAR_MOUSE` receives callback
- IconHover starts vsync-locked frame timer (~8 ms interval)
- Each tick: compute scales → set DComp transforms → `Commit()`
- On mouse leave: settle animation runs (~150 ms)
- Timer self-cancels when all icons reach scale 1.0
- Returns to idle state

---

## 1.10 Shutdown Sequence

```
1. User clicks Exit in tray menu (or Tray App receives WM_CLOSE)
2. Tray App sends TE_IPC_MSG_SHUTDOWN via Named Pipe
3. Engine IPC server receives SHUTDOWN command
4. Engine posts WM_TE_IPC_COMMAND(SHUTDOWN) to UI thread
5. TE_CoreManagerShutdown() runs on UI thread:
   a. Disable all plugins in reverse priority order (each Disable() must fully revert)
   b. Shutdown all plugins in reverse priority order
   c. FreeLibrary for each plugin DLL
   d. Stop config watcher thread
   e. Stop IPC server thread
   f. Shutdown logger (flush remaining entries)
   g. RemoveWindowSubclass from Shell_TrayWnd
   h. Free TE_CoreState
   i. Send TE_IPC_MSG_SHUTDOWN_COMPLETE back via pipe
6. Tray App receives SHUTDOWN_COMPLETE
7. Tray App calls UnhookWindowsHookEx
8. DLL unloads from Explorer
9. Tray App exits
```

---

## 1.11 Windows-Specific Mechanisms

| Mechanism | API | Purpose |
|---|---|---|
| DLL Injection | `SetWindowsHookEx(WH_CBT)` | Load Engine DLL into Explorer |
| Deferred Init | `PostMessage(WM_TE_INIT)` | Run init outside loader lock |
| Taskbar Subclassing | `SetWindowSubclass` (comctl32) | Intercept taskbar window messages |
| Config Hot-Reload | `ReadDirectoryChangesW` | Detect config file changes |
| IPC | Named Pipes (`CreateNamedPipeW`) | Tray App ↔ Engine communication |
| Crash Recovery | `WaitForSingleObject(explorer_handle)` + `TaskbarCreated` message | Detect Explorer death + re-inject |
| GPU Rendering | DirectComposition (`IDCompositionDevice`) | Hardware-accelerated visual overlay |
| Icon Discovery | UI Automation (`IUIAutomation`) | Read taskbar button positions |
| Icon Bitmaps | `SHGetImageList(SHIL_JUMBO)` + `IExtractIcon` | Capture icon images |
| Work Area | `SystemParametersInfoW(SPI_SETWORKAREA)` | Adjust desktop for resized taskbar |
| DPI | Per-monitor DPI v2 (`DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`) | Correct scaling |
| Auto-Start | Task Scheduler (`schtasks` at user logon) | Start on login |
| Tray Icon | `Shell_NotifyIconW` | System tray presence |
| Fault Isolation | SEH (`__try/__except`) | Catch plugin crashes |

---

## 1.12 External Dependencies

| Dependency | Type | Purpose | Size |
|---|---|---|---|
| `cJSON` | Vendored C source (single file) | JSONC config parsing | ~1000 LOC |
| `Catch2` | CMake FetchContent (dev only) | Unit testing | Header-only |
| `Google Benchmark` | CMake FetchContent (dev only) | Micro-benchmarks | Library |
| Windows App SDK | NuGet / system | WinUI 3 GUI hosting | Runtime |

**No other runtime dependencies.** The Engine DLL and plugin DLLs link only against Windows system DLLs.

---

## 1.13 Source Tree Structure

```
TaskbarEngine/
├── CMakeLists.txt                  # Root CMake workspace
├── CMakePresets.json               # Dual toolchain presets (MSVC + Clang-cl)
├── Doxyfile                        # Doxygen configuration
├── azure-pipelines.yml             # Azure DevOps CI/CD
├── build.bat / run.bat             # Developer convenience scripts
├── README.md                       # User-facing project overview
├── LICENSE                         # MIT
│
├── SDK/                            # Shared static library + ABI headers
│   ├── CMakeLists.txt              # te_sdk static lib target
│   ├── include/sdk/                # Public headers (included by all components)
│   │   ├── te_types.h              # Core types, HRESULT macros, TE_API_VERSION
│   │   ├── te_plugin.h             # PluginInterface, PluginContext, SettingDescriptor
│   │   ├── te_events.h             # TE_EventType enum, event payload structs
│   │   ├── te_ipc.h                # IPC header struct, message types, serialization
│   │   ├── te_jsonc.h              # JSONC parse API (cJSON wrapper)
│   │   ├── te_log.h                # Log level enum, LogFunc typedef
│   │   ├── te_version.h            # RtlGetVersion wrapper, build compatibility
│   │   ├── te_easing.h             # Animation easing curves (Gaussian, Cubic, etc.)
│   │   └── te_dpi.h                # TE_ScaleDPI helper
│   └── src/                        # SDK implementation
│       ├── te_jsonc.c              # Comment stripper + cJSON wrapper
│       ├── te_log.c                # Log write forwarding
│       ├── te_log_impl.c           # Ring buffer + background flush thread
│       ├── te_dpi.c                # DPI scaling utility
│       ├── te_easing.c             # Easing curve math
│       ├── te_ipc.c                # IPC serialization/deserialization
│       ├── te_version.c            # RtlGetVersion wrapper
│       └── te_state_store.c        # SRWLock hash map state store
│
├── Core/                           # Engine DLL (injected into Explorer)
│   ├── CMakeLists.txt              # EngineDLL.dll shared library target
│   ├── include/core/               # Internal headers
│   │   ├── core_manager.h
│   │   ├── engine.h
│   │   ├── config.h
│   │   ├── config_watcher.h
│   │   ├── event_dispatch.h
│   │   ├── plugin_loader.h
│   │   ├── fault_isolation.h
│   │   ├── taskbar_subclass.h
│   │   ├── ipc_server.h
│   │   ├── ipc_protocol.h
│   │   ├── shell_hook.h
│   │   ├── power_device.h
│   │   ├── state_store.h
│   │   └── vdesktop_notify.h
│   └── src/
│       ├── dllmain.c               # DllMain + process guard + Phase A
│       ├── engine_init.c           # Phase B initialization
│       ├── core_manager.c          # Top-level orchestration
│       ├── config.c                # JSONC loading, validation, defaults
│       ├── config_watcher.c        # ReadDirectoryChangesW + debounce
│       ├── event_dispatch.c        # Subscription table + sync dispatch
│       ├── plugin_loader.c         # Directory scan, LoadLibrary, lifecycle
│       ├── fault_isolation.c       # SEH wrapper + watchdog timer
│       ├── taskbar_subclass.c      # SetWindowSubclass on Shell_TrayWnd
│       ├── ipc_server.c            # Named pipe server + protocol handler
│       ├── ipc_protocol.c          # Message serialization
│       ├── shell_hook.c            # RegisterShellHookWindow
│       ├── power_device.c          # Power + device notifications
│       └── vdesktop_notify.cpp     # IVirtualDesktopNotification (COM, C++)
│
├── App/                            # Tray App (out-of-process host)
│   ├── CMakeLists.txt              # TaskbarEngine.exe target
│   ├── include/app/
│   │   ├── tray.h
│   │   ├── tray_menu.h
│   │   ├── ipc_client.h
│   │   └── crash_recovery.h
│   ├── src/
│   │   ├── main.c                  # WinMain, message pump, hook install
│   │   ├── tray.c                  # Shell_NotifyIcon
│   │   ├── tray_menu.c             # TrackPopupMenu context menu
│   │   ├── ipc_client.c            # Named pipe client
│   │   ├── crash_recovery.c        # Explorer PID monitor + TaskbarCreated
│   │   ├── gui_main.cpp            # WinUI 3 window creation (C++/WinRT)
│   │   ├── settings_page.cpp       # Auto-generated settings from metadata
│   │   ├── about_page.cpp          # About page with version info
│   │   └── scheduler.c             # Task Scheduler registration
│   └── res/
│       ├── app.manifest            # DPI awareness, COM, version
│       └── app.rc                  # Icon resource
│
├── Modules/                        # Plugin DLLs (one directory per plugin)
│   ├── taskbar_resize/
│   │   ├── CMakeLists.txt
│   │   ├── taskbar_resize.c        # Plugin implementation
│   │   └── taskbar_resize.h
│   ├── icon_hover/
│   │   ├── CMakeLists.txt
│   │   ├── icon_hover.c            # Plugin ABI entry points (C)
│   │   ├── icon_hover_internal.h
│   │   ├── magnification.c / .h    # 4 curve functions (pure math, C)
│   │   ├── icon_layout.c / .h      # Icon coordinate mapping
│   │   ├── uia_discovery.cpp / .h  # UIA taskbar element enumeration
│   │   ├── icon_capture.cpp / .h   # IImageList icon extraction + cache
│   │   ├── dcomp_overlay.cpp / .h  # DirectComposition visual tree
│   │   └── frame_loop.cpp / .h     # Vsync timer + commit loop
│   ├── dummy/                      # Test mock plugin (dev only)
│   └── fault/                      # Fault-injection plugin (dev only)
│
├── Tests/                          # Catch2 unit tests
│   ├── CMakeLists.txt
│   └── test_*.cpp                  # One file per module
│
├── Benchmarks/                     # Google Benchmark + system harness
│   ├── CMakeLists.txt
│   └── bench_*.cpp
│
├── Config/                         # Default config and schema
│   ├── default_config.jsonc
│   └── settings_schema.json
│
├── Scripts/                        # Build/packaging/CI helpers
│   ├── package.ps1
│   └── uninstall.ps1
│
├── ThirdParty/                     # Vendored dependencies
│   └── cJSON/
│       ├── cJSON.c
│       └── cJSON.h
│
└── docs/                           # Design and reference documentation
    ├── design_decisions.md          # Source of truth (from design interview)
    ├── phases/                      # Phase-by-phase implementation specs
    └── 00-19 series                 # Architecture documentation
```
