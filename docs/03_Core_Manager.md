# 03 — Core Manager

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: `Core/` (`EngineDLL.dll`)

---

## 1. Role & Architectural Responsibilities

The **Core Manager** (`core_manager.c` / `core_manager.h`) is the central orchestrator of TaskbarEngine inside `explorer.exe`. It manages the singleton execution context, coordinates plugin loading and unloading, routes window messages and events, maintains the shared blackboard state store, monitors filesystem configuration changes, and serves IPC requests.

```mermaid
flowchart TD
    ShellTray["Shell_TrayWnd HWND"] -->|Subclass Messages| SubclassProc["taskbar_subclass.c"]
    SubclassProc -->|WM_TE_INIT| PhaseB["InitPhaseB()"]
    SubclassProc -->|WM_TE_IPC_COMMAND| IPCMarshal["IPC Command Handler"]
    SubclassProc -->|Subscribed Msgs (e.g. WM_MOUSEMOVE)| MsgFilter["te_msg_filter.c"]

    PhaseB --> Config["config.c (Load & Validate)"]
    PhaseB --> Watcher["config_watcher.c (ReadDirectoryChangesW)"]
    PhaseB --> PluginLoader["plugin_loader.c (Scan Modules/)"]
    PhaseB --> IPCServer["ipc_server.c (Named Pipe Listener)"]

    PluginLoader --> CoreState["TE_CoreState (Singleton)"]
    CoreState --> StateStore["state_store.c (Inter-Plugin Blackboard)"]
    CoreState --> EventBus["event_dispatch.c (Synchronous Dispatch)"]
    CoreState --> TimerQueue["te_timer.c (UI Thread Timers)"]
```

---

## 2. Singleton State Container (`TE_CoreState`)

All engine state is encapsulated inside an opaque heap allocation created during Phase A:

```c
struct TE_CoreState {
    HINSTANCE           hinstance;              /* Module instance handle of EngineDLL.dll */
    HWND                taskbar_hwnd;           /* Primary Shell_TrayWnd handle */
    HMONITOR            primary_monitor;        /* Monitor hosting primary taskbar */
    uint32_t            dpi;                    /* Taskbar DPI scaling */
    cJSON*              config_root;            /* Current parsed JSONC configuration */
    SRWLOCK             config_lock;            /* Multi-reader single-writer lock for config */
    TE_PluginEntry*     plugins;                /* Dynamically sized array of loaded plugins */
    uint32_t            plugin_count;           /* Number of loaded plugins */
    uint32_t            plugin_capacity;        /* Allocated capacity of plugins array */
    uint32_t            current_plugin_id;      /* Active plugin ID for event attribution */
    TE_ConfigWatcher*   watcher;                /* Directory change notification handle */
    TE_IpcServer*       ipc_server;             /* IPC named pipe server instance */
    TE_StateStore*      state_store;            /* Shared blackboard data store */
    TE_MsgFilterTable*  msg_filter;             /* Subscribed window message registry */
    TE_TimerQueue*      timer_queue;            /* UI thread timer list */
};
```

---

## 3. Two-Phase Initialization Lifecycle

### Phase A: `TE_CoreManagerInitPhaseA(HINSTANCE hinstance)`
1. Verifies that the current process name contains `explorer.exe`.
2. Allocates and zeros `TE_CoreState`.
3. Locates `Shell_TrayWnd` via `FindWindowW(L"Shell_TrayWnd", NULL)`.
4. Installs the subclass procedure using `SetWindowSubclass(taskbar_hwnd, TE_TaskbarSubclassProc, TE_SUBCLASS_ID, (DWORD_PTR)state)`.
5. Posts `WM_TE_INIT` to `Shell_TrayWnd` to defer initialization outside the OS loader lock.
6. Returns `S_OK`.

### Phase B: `TE_CoreManagerInitPhaseB(void)`
1. Intercepts `WM_TE_INIT` on Explorer's main UI thread.
2. Resolves `%LOCALAPPDATA%\TaskbarEngine\config.jsonc`. If absent, copies `default_config.jsonc`.
3. Parses configuration using `TE_JsoncParse` into `config_root`.
4. Starts `config_watcher.c` on a background thread targeting `%LOCALAPPDATA%\TaskbarEngine\`.
5. Initializes the event dispatch table, state store, message filter table, and timer queue.
6. Scans the `Modules/` directory, discovers all `*.dll` files, calls `GetPluginInterface()`, validates compatibility, and registers each plugin.
7. Calls `Initialize()` and `Enable()` on all plugins whose configuration section is enabled.
8. Launches `ipc_server.c` on a dedicated thread listening on `\\.\pipe\TaskbarEngine`.

---

## 4. Subclassing & Message Routing

TaskbarEngine uses `comctl32.dll`'s `SetWindowSubclass` / `DefSubclassProc` rather than `SetWindowLongPtr(GWLP_WNDPROC)`:

- **Safety**: Guarantees safe un-subclassing even if other software subclasses the same window later.
- **Custom Messages Handled**:
  - `WM_TE_INIT`: Triggers Phase B initialization.
  - `WM_TE_IPC_COMMAND`: Synchronously executes IPC requests (schema queries, plugin toggles) on the UI thread.
  - `WM_TE_TIMER_TICK`: Fires expired timers registered via `register_timer`.
  - `WM_DPICHANGED` / `WM_DISPLAYCHANGE`: Dispatches engine events to plugins.
  - `WM_THEMECHANGED` / `WM_SETTINGCHANGE`: Broadcasts theme and system setting updates.
  - `WM_DESTROY`: Invokes `TE_CoreManagerShutdown` if the taskbar is destroyed.

---

## 5. Teardown & Shutdown Protocol

When Explorer shuts down, or when a graceful exit is requested via IPC (`TE_IPC_MSG_SHUTDOWN`):
1. **Disable Plugins**: Calls `Disable()` on all active plugins in reverse priority order.
2. **Shutdown Plugins**: Calls `Shutdown()` and frees library handles (`FreeLibrary`).
3. **Stop Watcher**: Closes `ReadDirectoryChangesW` handles and terminates background threads.
4. **Remove Subclass**: Calls `RemoveWindowSubclass(taskbar_hwnd, TE_TaskbarSubclassProc, TE_SUBCLASS_ID)`.
5. **Free Core State**: Releases state store, message filters, JSON configuration trees, and frees `TE_CoreState`.
