# Part 2 — Technical Implementation

> Technical Reconstruction Specification — TaskbarEngine

---

## 2.1 Win32 APIs — Core Usage Map

### DLL Injection

**What**: `SetWindowsHookEx(WH_CBT, hookProc, hDll, threadId)` installs a Computer-Based Training hook targeting a specific thread. When that thread next processes a message, Windows force-loads the hook DLL into the target process.

**Why TaskbarEngine needs it**: The Engine DLL must execute inside `explorer.exe` to subclass `Shell_TrayWnd` and access the taskbar's HWND, composition tree, and message pump.

**Where used**: `App/src/main.c` — Tray App calls `SetWindowsHookEx` targeting the `Shell_TrayWnd` owner thread.

**Important limitations**:
- WH_CBT injects into EVERY process that calls the hooked thread. The Engine DLL's `DllMain` must include a process guard (`if not explorer → return immediately`).
- The hook handle must remain alive for the DLL to stay loaded. `UnhookWindowsHookEx` triggers DLL unload.
- Antivirus heuristics flag `SetWindowsHookEx` + `explorer.exe` injection. Code signing is strongly recommended for distribution.
- Must post `WM_NULL` to `Shell_TrayWnd` immediately after hooking to force the message pump to process the hook and trigger DLL load. Without this, the DLL may not load until Explorer naturally processes a CBT event.

### Deferred Initialization

**What**: `PostMessage(Shell_TrayWnd, WM_APP+100, 0, 0)` queues a custom message that Explorer's message pump will process on its main thread.

**Why TaskbarEngine needs it**: `DllMain` runs under the OS loader lock. Calling `LoadLibrary`, `CreateThread`, `CoInitializeEx`, or any blocking API from `DllMain` causes deadlocks or undefined behavior. All real initialization must happen outside the loader lock.

**Where used**: `Core/src/dllmain.c` — Phase A posts message; `Core/src/taskbar_subclass.c` intercepts `WM_TE_INIT` and calls `TE_CoreManagerInitPhaseB()`.

**Important limitations**:
- `PostMessage` is asynchronous; there is no guarantee when the message is processed. If Explorer's pump is busy, initialization is delayed.
- The message ID (`WM_APP+100` or similar) must not collide with other software using `WM_APP`-range messages on `Shell_TrayWnd`.
- If `Shell_TrayWnd` is destroyed before the message is processed, initialization never occurs.

### Window Subclassing

**What**: `SetWindowSubclass(hwnd, proc, id, refData)` from `comctl32.dll` installs a subclass procedure on a window. The subclass receives all messages before the original `WndProc`. Must call `DefSubclassProc` for unhandled messages.

**Why TaskbarEngine needs it**: The Engine must intercept messages like `WM_WINDOWPOSCHANGING` (for resize), `WM_MOUSEMOVE` (for hover), `WM_DPICHANGED`, `WM_DISPLAYCHANGE`, and custom `WM_TE_*` messages on the taskbar window.

**Where used**: `Core/src/taskbar_subclass.c` — installed during Phase A, removed during shutdown.

**Important limitations**:
- `SetWindowSubclass` is safe for multi-subclass scenarios (unlike `SetWindowLongPtr(GWLP_WNDPROC)` which breaks chaining).
- The subclass proc runs on the window's owning thread (Explorer's main UI thread). All code in the subclass proc must be fast (<1 ms).
- Must remove subclass via `RemoveWindowSubclass` before DLL unload, otherwise Explorer will call into freed memory.
- On `WM_NCDESTROY`, the subclass is automatically removed by `comctl32`. The engine should detect this and not attempt double-removal.

### Configuration File Watching

**What**: `ReadDirectoryChangesW(hDir, buffer, size, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE, ...)` monitors a directory for file modifications.

**Why TaskbarEngine needs it**: When the user (or the GUI) edits `config.jsonc`, the Engine must detect the change and hot-reload within ~200 ms.

**Where used**: `Core/src/config_watcher.c` — background thread waits on `ReadDirectoryChangesW`, triggers debounced reload.

**Important limitations**:
- Text editors (VS Code, Notepad++) write files in multiple steps (write temp → rename → delete old), generating 2-3 change events. The 100 ms debounce timer coalesces these.
- `ReadDirectoryChangesW` requires a directory handle opened with `FILE_LIST_DIRECTORY` access.
- The buffer must remain valid until the I/O completes. Use overlapped I/O or a dedicated waiting thread.

### Named Pipe IPC

**What**: `CreateNamedPipeW("\\\\.\\pipe\\TaskbarEngine", ...)` creates a local inter-process communication channel. `ConnectNamedPipe` + `ReadFile` / `WriteFile` for message exchange.

**Why TaskbarEngine needs it**: The Tray App (and Settings GUI) need to send commands to the Engine DLL running inside Explorer. Named pipes provide full-duplex, low-latency, local-only communication.

**Where used**:
- `Core/src/ipc_server.c` — Engine-side pipe server with async overlapped I/O
- `App/src/ipc_client.c` — Tray App / GUI client

**Important limitations**:
- The pipe must have a restricted DACL (current user SID only) to prevent other processes from sending commands.
- The pipe server thread must NEVER directly mutate `TE_CoreState`. All state-changing commands must be marshaled to the UI thread via `PostMessage(taskbar_hwnd, WM_TE_IPC_COMMAND, cmd, payload)`. This is a P0 requirement from the technical audit.
- Max 1 concurrent client connection (the Tray App / GUI).
- Payload max: 64 KB per message.

---

## 2.2 Window Management

### Shell_TrayWnd Discovery

```c
HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
```

- Returns the primary taskbar window handle.
- Secondary monitors use `Shell_SecondaryTrayWnd` (discovered via `FindWindowExW` or `EnumWindows`).
- The HWND changes when Explorer restarts. After crash recovery, re-discover via the `TaskbarCreated` registered message.

### Overlay Window (DirectComposition Target)

The IconHover plugin creates a child window of `Shell_TrayWnd` for the DirectComposition visual tree:

```c
HWND overlay = CreateWindowExW(
    WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
    L"TE_OverlayClass", NULL,
    WS_CHILD | WS_VISIBLE,
    0, 0, width, height,
    taskbar_hwnd, NULL, hinstance, NULL
);
```

- `WS_EX_LAYERED`: Enables per-pixel alpha (overlay is invisible when alpha=0).
- `WS_EX_TRANSPARENT`: Mouse clicks pass through to the real taskbar.
- `WS_EX_TOPMOST`: Ensures overlay renders above the taskbar's XAML content.
- `WS_CHILD`: Overlay moves with the taskbar and inherits its coordinate space.

### Desktop Work Area Update

When TaskbarResize changes the taskbar height:

```c
RECT work_area;
SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
work_area.bottom = screen_height - new_taskbar_height;
SystemParametersInfoW(SPI_SETWORKAREA, 0, &work_area, SPIF_SENDCHANGE);
```

`Disable()` must restore the original work area.

---

## 2.3 Windows Messages and Events

### Messages Handled by the Subclass Proc

| Message | Handler | Purpose |
|---|---|---|
| `WM_TE_INIT` (custom) | Phase B init | Deferred initialization trigger |
| `WM_TE_IPC_COMMAND` (custom) | IPC command handler | Execute IPC requests on UI thread |
| `WM_TE_TIMER_TICK` (custom) | Timer dispatch | Fire expired plugin timers |
| `WM_WINDOWPOSCHANGING` | TaskbarResize | Enforce custom taskbar height |
| `WM_MOUSEMOVE` | IconHover (subscribed) | Mouse position for magnification |
| `WM_MOUSELEAVE` | IconHover (subscribed) | Trigger settle animation |
| `WM_DPICHANGED` | Core Manager | Dispatch DPI change to plugins |
| `WM_DISPLAYCHANGE` | Core Manager | Dispatch display change to plugins |
| `WM_THEMECHANGED` | Core Manager | Theme update notification |
| `WM_SETTINGCHANGE` | Core Manager | System setting update |
| `WM_DESTROY` | Core Manager | Emergency shutdown |
| `WM_ENDSESSION` | Core Manager | Graceful shutdown on OS shutdown |

### Seven Event Sources

The Engine collects events from 7 distinct sources and normalizes them into `TE_EventType` values:

| # | Source | API | Events Generated |
|---|---|---|---|
| 1 | Taskbar subclass | `SetWindowSubclass` | `TE_EVENT_TASKBAR_GEOMETRY`, `TE_EVENT_TASKBAR_MOUSE`, `TE_EVENT_DPI_CHANGED`, `TE_EVENT_DISPLAY_CHANGED` |
| 2 | Shell hook | `RegisterShellHookWindow` | `TE_EVENT_SHELL_HOOK` (app open/close/focus) |
| 3 | Power notification | `RegisterPowerSettingNotification` | `TE_EVENT_POWER` (suspend/resume, battery) |
| 4 | Device notification | `RegisterDeviceNotification` | `TE_EVENT_DEVICE` (monitor plug/unplug) |
| 5 | Config watcher | `ReadDirectoryChangesW` | `TE_EVENT_CONFIG_CHANGED` |
| 6 | Named pipe | `CreateNamedPipeW` | IPC commands (not an event type; handled directly) |
| 7 | Virtual desktop | `IVirtualDesktopNotification` (COM) | `TE_EVENT_VDESKTOP` (desktop switch) |

### Event Dispatch Model

Synchronous callback dispatch:

```c
for (int i = 0; i < dispatch_table_count; i++) {
    if (dispatch_table[i].event_type == event_type) {
        __try {
            dispatch_table[i].callback(event_type, event_data, dispatch_table[i].user_data);
        } __except (TE_FaultFilter(GetExceptionInformation(), plugin_name)) {
            /* disable faulting plugin */
        }
    }
}
```

- Direct function pointer call (no queuing, no async)
- All dispatch runs on Explorer's UI thread
- SEH wraps each callback
- Plugins subscribe via `ctx->subscribe(event_type, callback, user_data)`
- Targeted dispatch by plugin ID is supported via `TE_EventDispatchTargeted()`

---

## 2.4 Taskbar Integration

### TaskbarResize Plugin Implementation

**Geometry mutation via `WM_WINDOWPOSCHANGING` interception:**

```c
case WM_WINDOWPOSCHANGING: {
    WINDOWPOS* wp = (WINDOWPOS*)lParam;
    if (!(wp->flags & SWP_NOSIZE)) {
        wp->cy = TE_ScaleDPI(config_height, current_dpi);
    }
    break;
}
```

This modifies the `WINDOWPOS` struct BEFORE Explorer applies it, preventing flickering.

**Initial resize on Enable():**

```c
SetWindowPos(taskbar_hwnd, NULL, 0, 0, 0, new_height,
    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
```

**Disable() must restore:**
1. Original taskbar height via `SetWindowPos`
2. Original desktop work area via `SPI_SETWORKAREA`
3. Unsubscribe from `WM_WINDOWPOSCHANGING`

**Multi-monitor**: Maintain per-monitor state array `{ HMONITOR, HWND taskbar, RECT original_pos, int original_height, int target_height }`. Secondary taskbars use `Shell_SecondaryTrayWnd`.

### IconHover Plugin Implementation

**Architecture (6 sub-modules):**

1. **`icon_hover.c`** — Plugin ABI entry points (C), lifecycle orchestration
2. **`magnification.c`** — Pure math: 4 curve functions (Gaussian, Cubic, Cosine, Linear)
3. **`icon_layout.c`** — Icon coordinate mapping from UIA discovery results
4. **`uia_discovery.cpp`** — COM/UIA: enumerate taskbar buttons, extract bounding rects
5. **`icon_capture.cpp`** — `SHGetImageList(SHIL_JUMBO)` + Direct2D/WIC surface capture
6. **`dcomp_overlay.cpp`** — DirectComposition visual tree creation and management
7. **`frame_loop.cpp`** — Vsync-locked animation timer + `IDCompositionDevice::Commit()`

---

## 2.5 Rendering — DirectComposition Pipeline

### Visual Tree Structure

```
IDCompositionDevice (created via DCompositionCreateDevice)
└── IDCompositionTarget (bound to overlay HWND)
    └── Root IDCompositionVisual (covers entire taskbar)
        ├── Icon Visual 0 (IDCompositionVisual)
        │   ├── IDCompositionSurface (captured icon bitmap)
        │   ├── IDCompositionScaleTransform (magnification)
        │   └── IDCompositionTranslateTransform (position)
        ├── Icon Visual 1
        │   └── ...
        └── Icon Visual N
```

### Frame Loop

```
Animation Tick (timer callback, ~8 ms interval at 120 Hz):
  1. if mouse is in taskbar region:
     a. Get cursor position
     b. For each icon i:
        - d = |icon_center_x - cursor_x|
        - if d < radius: scale = 1.0 + (max_scale - 1.0) * W(d / radius)
        - else: scale = 1.0
     c. For each icon visual:
        - SetTransform(ScaleTransform(scale, scale))
        - SetTransform(TranslateTransform(adjusted_x, adjusted_y))
     d. IDCompositionDevice::Commit()
  2. if mouse left and settle animation active:
     a. Lerp all scales toward 1.0 using deltaTime
     b. Commit()
     c. if max(|scale - 1.0|) < epsilon: cancel timer
```

### Magnification Math

For icon at center position `x_i`, cursor at `x_cursor`, radius `R`, max scale `S`:

```
d = |x_i - x_cursor|
u = d / R  (normalized distance, clamped to [0, 1])

Gaussian:  W(u) = exp(-u² / (2 * 0.4²))     [σ = 0.4]
Cubic:     W(u) = (1 - u)² * (1 + 2u)
Cosine:    W(u) = (1 + cos(π * u)) / 2
Linear:    W(u) = 1 - u

final_scale = 1.0 + (S - 1.0) * W(u)
```

---

## 2.6 UI Automation (Icon Discovery)

```cpp
// Create UIA client
IUIAutomation* uia = nullptr;
CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
    IID_IUIAutomation, (void**)&uia);

// Get taskbar element
IUIAutomationElement* taskbar_elem = nullptr;
uia->ElementFromHandle(taskbar_hwnd, &taskbar_elem);

// Find task list buttons
IUIAutomationCondition* button_cond = nullptr;
uia->CreatePropertyCondition(UIA_ControlTypePropertyId,
    CComVariant(UIA_ButtonControlTypeId), &button_cond);

IUIAutomationElementArray* buttons = nullptr;
taskbar_elem->FindAll(TreeScope_Descendants, button_cond, &buttons);

// Extract bounding rects per button
for (int i = 0; i < count; i++) {
    IUIAutomationElement* btn;
    buttons->GetElement(i, &btn);
    RECT rect;
    btn->get_CurrentBoundingRectangle(&rect);
    // Store rect + automation ID for icon matching
}
```

**Important limitations**:
- UIA tree structure has changed between Windows 11 builds (22H2 vs 23H2 vs 24H2).
- UIA queries take 10–50 ms — NEVER call during the animation frame loop.
- Cache results aggressively. Refresh only on `TE_EVENT_SHELL_HOOK` (app open/close).
- Max 2 UIA enumerations per second.

---

## 2.7 Icon Bitmap Capture

```cpp
// Get system image list (JUMBO = 256x256)
IImageList* image_list = nullptr;
SHGetImageList(SHIL_JUMBO, IID_IImageList, (void**)&image_list);

// Extract icon for an app
HICON hicon = nullptr;
image_list->GetIcon(icon_index, ILD_TRANSPARENT, &hicon);

// Convert HICON to DirectComposition surface
// (via ID2D1DeviceContext::DrawIcon or manual BitBlt + CreateBitmapFromHBITMAP)
```

- Request `SHIL_JUMBO` (256×256) and scale down to avoid blurriness at high DPI.
- Cache captured bitmaps as `IDCompositionSurface` textures.
- Invalidate cache on `TE_EVENT_SHELL_HOOK` (app icon change).
- Never re-capture during animation frames.

---

## 2.8 Configuration System

### JSONC Parsing Pipeline

```
1. Read file into memory buffer
2. Strip // and /* */ comments (te_jsonc.c comment stripper)
3. Pass clean JSON to cJSON_Parse()
4. Return cJSON* root tree
```

### Config Access API

```c
HRESULT TE_ConfigLoad(const wchar_t* path, cJSON** out_root);
const cJSON* TE_ConfigGetPluginSection(const cJSON* root, const char* name);
int   TE_ConfigGetInt(const cJSON* section, const char* key, int default_val);
float TE_ConfigGetFloat(const cJSON* section, const char* key, float default_val);
bool  TE_ConfigGetBool(const cJSON* section, const char* key, bool default_val);
const char* TE_ConfigGetString(const cJSON* section, const char* key, const char* default_val);
```

### Hot-Reload Sequence

```
ReadDirectoryChangesW detects FILE_NOTIFY_CHANGE_LAST_WRITE
  → 100 ms debounce timer starts (CreateTimerQueueTimer)
  → Timer fires: TE_JsoncParse(config_path) into temp cJSON*
  → If parse fails: log error, keep old config, return
  → Config diff: compare old vs new per-plugin sub-objects
  → Atomic swap: config_root = new_config; cJSON_Delete(old_config)
  → For each changed plugin section:
    - If enabled changed false→true: call Enable()
    - If enabled changed true→false: call Disable()
    - If other values changed: dispatch TE_EVENT_CONFIG_CHANGED
```

---

## 2.9 IPC Protocol

### Message Types

```c
typedef enum TE_IpcMsgType {
    TE_IPC_MSG_SHUTDOWN = 1,
    TE_IPC_MSG_SHUTDOWN_COMPLETE,
    TE_IPC_MSG_GET_PLUGIN_LIST,
    TE_IPC_MSG_PLUGIN_LIST_RESPONSE,
    TE_IPC_MSG_ENABLE_PLUGIN,
    TE_IPC_MSG_DISABLE_PLUGIN,
    TE_IPC_MSG_RELOAD_CONFIG,
    TE_IPC_MSG_GET_SETTINGS,
    TE_IPC_MSG_SETTINGS_RESPONSE
} TE_IpcMsgType;
```

### Wire Format

```
+-------+-------+-------+----------------+-------------------+
| magic | ver   | type  | payload_length | payload (0..64KB) |
| 4B    | 4B    | 4B    | 4B             | variable          |
+-------+-------+-------+----------------+-------------------+
  'TEIP'   1    enum     N bytes
```

### Security

- Pipe ACL restricted to current user SID via `SECURITY_ATTRIBUTES` + explicit DACL.
- Magic number validation prevents accidental cross-application pipe connections.
- No authentication beyond DACL (the technical audit notes this as a known limitation).

---

## 2.10 Crash Recovery

### State Machine

```
RUNNING
  → Explorer process terminates (WaitForSingleObject signals)
  → EXPLORER_DEAD

EXPLORER_DEAD
  → Windows automatically restarts Explorer
  → Tray App receives TaskbarCreated registered window message
  → WAITING_TASKBAR_CREATED

WAITING_TASKBAR_CREATED
  → FindWindowW(L"Shell_TrayWnd") succeeds
  → Re-install WH_CBT hook targeting new Explorer thread
  → REHOOKING

REHOOKING
  → Engine DLL re-loads into new Explorer process
  → DllMain Phase A + Phase B re-initialize
  → RUNNING
```

### Implementation

```c
// Background thread in Tray App
HANDLE hExplorer = OpenProcess(SYNCHRONIZE, FALSE, explorer_pid);
WaitForSingleObject(hExplorer, INFINITE);  // blocks until Explorer dies
CloseHandle(hExplorer);
// Explorer is dead — wait for TaskbarCreated
// (handled in the Tray App's message pump via RegisterWindowMessage(L"TaskbarCreated"))
```

---

## 2.11 Settings GUI (WinUI 3)

### Auto-Generation Pipeline

```
1. GUI sends TE_IPC_MSG_GET_SETTINGS via Named Pipe
2. Engine iterates all loaded plugins, calls GetSettings() on each
3. Engine serializes SettingDescriptor arrays as JSON payload
4. Engine sends TE_IPC_MSG_SETTINGS_RESPONSE back
5. GUI parses response, creates WinUI 3 controls:
   - TE_SETTING_BOOL → ToggleSwitch
   - TE_SETTING_INT → NumberBox (with min/max/step)
   - TE_SETTING_FLOAT → Slider (with min/max/step)
   - TE_SETTING_STRING → TextBox
   - TE_SETTING_ENUM → ComboBox
   - TE_SETTING_COLOR → ColorPicker
6. Controls populated with current values from config.jsonc
7. On user change: write config atomically, send RELOAD_CONFIG
```

### Atomic Config Write

```cpp
// Write to temp file
std::ofstream tmp(config_path + L".tmp." + std::to_wstring(GetCurrentProcessId()));
tmp << formatted_json;
tmp.close();

// Atomic replace
MoveFileExW(tmp_path, config_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
```

---

## 2.12 Logging Implementation

### Ring Buffer Design

```
+------+------+------+------+------+------+------+------+
| E0   | E1   | E2   | E3   | E4   | ...  | E_N  | (wrap)
+------+------+------+------+------+------+------+------+
  ^write_pos (atomic)                          ^read_pos
```

- 64 KB pre-allocated circular buffer
- Fixed-size entries (level + timestamp + message)
- `InterlockedCompareExchange` to advance write position atomically
- Background flush thread reads from `read_pos` to `write_pos` every 100 ms
- On overflow: oldest entries are overwritten (drop policy)
- Output format: `[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [module] message`
- File rotation: keep last 5 files, max 5 MB each
