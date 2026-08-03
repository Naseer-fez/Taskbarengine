> Source of truth: Extracted from the implementation roadmap.

# Phase 3 — TaskbarResize Plugin + IPC + Crash Recovery

---

## Purpose

Deliver the first **real, user-visible feature** (dynamic taskbar resize) along
with the **operational infrastructure** (named pipe IPC, Explorer crash recovery,
graceful shutdown) that makes the system production-usable.

Phase 3 ends with a **complete, deployable system** for one feature.

## High-Level Goal

A user can:

1. Run `TaskbarEngine.exe`.
2. Edit `config.jsonc` to set `"height": 24` under `taskbar_resize`.
3. See the taskbar resize immediately (hot-reload).
4. Restart Explorer — the system recovers automatically.
5. Right-click the tray icon → Exit — the taskbar returns to default height.

## Why This Phase Comes Now

The Core Manager (Phase 2) provides all the infrastructure plugins need. The
first plugin should be the simpler of the two v1 features. IPC and crash
recovery are bundled because they're needed for the system to be usable (without
IPC, there's no clean shutdown; without crash recovery, Explorer restarts break
everything).

## Components Implemented

| Component | Language | Description |
|---|---|---|
| TaskbarResize plugin | C | `Modules/taskbar_resize/taskbar_resize.c` — subclass `Shell_TrayWnd`, intercept `WM_WINDOWPOSCHANGING`, enforce height, update `SPI_SETWORKAREA`, handle multi-monitor via per-monitor state array, handle DPI changes |
| Named pipe server (Engine side) | C | Binary protocol server in Engine DLL. Creates `\\.\pipe\TaskbarEngine`. Async I/O via overlapped reads. Processes `SHUTDOWN`, `GET_PLUGIN_LIST`, `ENABLE_PLUGIN`, `DISABLE_PLUGIN`, `RELOAD_CONFIG` commands. |
| Named pipe client (Tray App side) | C | Client in `TaskbarEngine.exe`. Sends commands, receives responses. Used by tray menu actions. |
| Binary IPC protocol | C | Header struct `{ magic, version, type, payload_length }` + payload. Message types enum. Serialization/deserialization functions. |
| Tray context menu | C | `TrackPopupMenu` with: Settings (placeholder), Enable/Disable All, Reload Config, About, Exit |
| Explorer crash recovery | C | Background thread: `WaitForSingleObject(hExplorer, INFINITE)`. On signal: wait for `TaskbarCreated` message → re-install WH_CBT hook → Engine DLL re-initializes. |
| Graceful shutdown | C | Tray App sends `SHUTDOWN` → Engine disables all plugins (reverse priority) → Engine sends `SHUTDOWN_COMPLETE` → Tray App calls `UnhookWindowsHookEx` → clean exit |
| Remaining event sources (5 of 7) | C/C++ | Shell hook, Power notification, Device notification, Pipe listener, `IVirtualDesktopNotification` |
| Micro-benchmark harness | C++ | Google Benchmark integration for hot-path functions |

## Folder Structure Additions

```diff
 TaskbarEngine/
 ├── Core/
 │   ├── src/
+│   │   ├── ipc_server.c          # Named pipe server + protocol handler
+│   │   ├── ipc_protocol.c        # Message serialization/deserialization
+│   │   ├── shell_hook.c          # RegisterShellHookWindow event source
+│   │   ├── power_device.c        # Power + device notification sources
+│   │   └── vdesktop_notify.cpp   # IVirtualDesktopNotification (COM, C++)
 │   └── include/
 │       └── core/
+│           ├── ipc_server.h
+│           ├── ipc_protocol.h
+│           ├── shell_hook.h
+│           ├── power_device.h
+│           └── vdesktop_notify.h
 ├── App/
 │   ├── src/
+│   │   ├── ipc_client.c          # Named pipe client
+│   │   ├── tray_menu.c           # Context menu handling
+│   │   └── crash_recovery.c      # Explorer PID monitor + TaskbarCreated
 │   └── include/
 │       └── app/
+│           ├── ipc_client.h
+│           ├── tray_menu.h
+│           └── crash_recovery.h
+├── Modules/
+│   └── taskbar_resize/
+│       ├── CMakeLists.txt
+│       ├── taskbar_resize.c      # Plugin implementation
+│       └── taskbar_resize.h      # Internal types
 ├── SDK/
 │   └── include/
 │       └── sdk/
+│           └── te_ipc.h          # IPC message type enum, header struct
 ├── Tests/
+│   ├── test_ipc_protocol.cpp     # Protocol serialization round-trip tests
+│   ├── test_taskbar_resize.cpp   # Resize logic unit tests (mock HWND)
+│   └── test_crash_recovery.cpp   # State machine tests
 ├── Benchmarks/
+│   ├── CMakeLists.txt
+│   └── bench_event_dispatch.cpp  # Google Benchmark: event dispatch latency
```

## Public APIs Introduced

| Header | API Surface |
|---|---|
| `te_ipc.h` | `TE_IpcMsgType` enum, `TE_IpcHeader` struct, `TE_IPC_MAGIC`, `TE_IPC_VERSION` constants |
| TaskbarResize settings | `GetSettings()` returns: `height` (int, 24-72, default 40), `padding` (int, 0-20, default 4), `margins` (int, 0-20, default 0), `icon_spacing` (int, 0-32, default 8) |

## Internal Systems Created

| System | Description |
|---|---|
| Named pipe server | Async overlapped I/O pipe `\\.\pipe\TaskbarEngine` with restricted ACL (current user SID only). Max 1 client (Tray App). |
| IPC binary protocol | 16-byte header + variable payload. 8 message types initially. |
| Crash recovery state machine | States: `RUNNING` → `EXPLORER_DEAD` → `WAITING_TASKBAR_CREATED` → `REHOOKING` → `RUNNING` |
| TaskbarResize per-monitor state | Array of `{ HMONITOR, HWND taskbar, RECT original_pos, int original_height, int target_height }` |

## Dependencies on Previous Phases

| Dependency | Source |
|---|---|
| Core Manager (config, events, plugin lifecycle) | Phase 2 |
| Event dispatch + subscription API | Phase 2 |
| Config hot-reload | Phase 2 |
| SEH + watchdog fault isolation | Phase 2 |
| Shell_TrayWnd subclass infrastructure | Phase 2 |
| Logging system | Phase 2 |

## Unit Testing Strategy

- `test_ipc_protocol.cpp`: Serialize each message type, deserialize, verify round-trip fidelity. Test malformed messages (truncated header, wrong magic, oversized payload).
- `test_taskbar_resize.cpp`: Unit test the height clamping logic, DPI scaling of height values, `WM_WINDOWPOSCHANGING` struct modification. (Note: actual taskbar manipulation requires integration testing on a real system.)
- `test_crash_recovery.cpp`: Test the crash recovery state machine transitions.
- **Manual integration test**: Run the full system, kill Explorer (`taskkill /f /im explorer.exe`), verify recovery.
- **Benchmark**: `bench_event_dispatch.cpp` — measure event dispatch latency per subscriber.

## Performance Considerations

- `WM_WINDOWPOSCHANGING` handler must complete in < 1 μs (just modify the `WINDOWPOS` struct).
- Named pipe server must not block Explorer's message pump (async overlapped I/O).
- Crash recovery background thread consumes zero CPU while waiting (`WaitForSingleObject` is kernel-efficient).
- TaskbarResize should add < 100 KB to RSS.

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Explorer fights back against `SetWindowPos` during DPI changes | Medium — flickering | Intercept `WM_WINDOWPOSCHANGING` to override Explorer's size *before* it takes effect. |
| `SPI_SETWORKAREA` affects all applications' window positioning | Medium — side effects | Save/restore original work area in `Disable()`. Test with maximized windows. |
| Named pipe blocked by firewall/security software | Low — IPC fails | Named pipes are local-only; firewalls typically don't block them. |
| Explorer restart changes `Shell_TrayWnd` HWND | Expected — handled | Crash recovery re-discovers HWND after `TaskbarCreated` message. |

## Deliverables

- [ ] TaskbarResize plugin resizes taskbar to configured height on enable
- [ ] Height changes via config hot-reload take effect within 200ms
- [ ] `Disable()` restores original taskbar height and work area
- [ ] Named pipe IPC working between Tray App and Engine
- [ ] Tray context menu: Exit cleanly shuts down the system
- [ ] Explorer crash recovery works (kill + automatic re-injection)
- [ ] All 7 event sources registered and dispatching
- [ ] Google Benchmark measuring event dispatch latency
- [ ] All unit tests pass

## Exit Criteria (Definition of Done)

> **Phase 3 is complete when:**
> 1. A user can run TaskbarEngine, see the taskbar resize to the configured height, edit the config, see the change live, and exit cleanly with the taskbar restored.
> 2. Killing Explorer and letting it restart results in automatic re-injection and re-application of the custom height within 2 seconds.
> 3. The IPC shutdown sequence completes without orphaning the DLL in Explorer (verified via Process Explorer).
> 4. Multi-monitor resize works correctly on a system with 2+ monitors at different DPIs.
> 5. Event dispatch micro-benchmark shows < 1 μs per callback invocation.
