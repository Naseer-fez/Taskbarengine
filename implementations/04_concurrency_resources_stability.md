# Part 4 — Concurrency, Resources, and Stability

> Technical Reconstruction Specification — TaskbarEngine

---

## 4.1 Thread Inventory

The Engine DLL creates the following persistent threads inside `explorer.exe`:

| # | Thread | Owner | Responsibility | Wakeup Mechanism | CPU When Idle |
|---|---|---|---|---|---|
| 1 | Explorer UI thread | Windows | Message pump, all plugin callbacks, DComp Commit | `GetMessage` | 0% |
| 2 | Log flush thread | `te_log_impl.c` | Drain ring buffer to file | `WaitForSingleObject` on event | 0% |
| 3 | Config watcher thread | `config_watcher.c` | Detect config file changes | `ReadDirectoryChangesW` (kernel I/O) | 0% |
| 4 | IPC server thread | `ipc_server.c` | Accept Named Pipe connections | `ConnectNamedPipe` (overlapped) | 0% |

**Design decision**: The architecture targets 3 Engine threads (main + log + one combined I/O thread). The current implementation uses 4 threads (config watcher and IPC server are separate). This is a documented violation of `AGENTS.md` Rule 6. The pragmatic resolution is either:
- (a) Merge config watcher and IPC server into a single I/O completion port thread, or
- (b) Formally document 4 threads as an accepted deviation.

The Tray App (`TaskbarEngine.exe`) has:

| # | Thread | Responsibility |
|---|---|---|
| 1 | Main thread | Message pump, tray icon, GUI hosting |
| 2 | Crash recovery thread | `WaitForSingleObject(explorer_handle, INFINITE)` |

---

## 4.2 Thread Ownership Rules

### UI Thread (Explorer's Main Thread)

**Owns**: All mutable engine state.

- All `TE_CoreState` mutations (config swap, plugin enable/disable, subscription changes)
- All plugin lifecycle calls (`Initialize`, `Enable`, `Disable`, `Update`, `Shutdown`)
- All event dispatch callbacks
- All `IDCompositionDevice::Commit()` calls
- All `SetWindowPos` / `SystemParametersInfoW` calls
- All `SetWindowSubclass` / `RemoveWindowSubclass` calls

**Rule**: If you need to change engine state from another thread, marshal via `PostMessage(taskbar_hwnd, WM_TE_IPC_COMMAND, ...)`. NEVER directly call state-mutating functions from background threads.

### Log Flush Thread

**Owns**: The file handle for the log file and the `read_pos` of the ring buffer.

- Reads entries from the ring buffer (lock-free via atomic `read_pos`)
- Writes formatted log lines to the log file
- Performs log file rotation
- Sleeps via `WaitForSingleObject` on a manual-reset event, woken by log writes or flush signals

**Rule**: Never accesses `TE_CoreState`, never calls plugin functions, never interacts with any HWND.

### Config Watcher Thread

**Owns**: The directory handle from `CreateFileW(config_dir, FILE_LIST_DIRECTORY, ...)`.

- Blocks on `ReadDirectoryChangesW`
- On file change: starts 100 ms debounce timer
- On timer expiry: posts `WM_TE_IPC_COMMAND(RELOAD_CONFIG)` to UI thread

**Rule**: Must NOT parse config or mutate state directly. Must marshal reload to UI thread.

### IPC Server Thread

**Owns**: The Named Pipe handle.

- Blocks on `ConnectNamedPipe` (overlapped)
- Reads messages via `ReadFile`
- Validates header (magic, version, type)
- **CRITICAL**: Must marshal ALL state-mutating commands to UI thread via `PostMessage`. This includes `RELOAD_CONFIG`, `ENABLE_PLUGIN`, `DISABLE_PLUGIN`, `SHUTDOWN`. Direct calls to `TE_CoreManagerReloadConfig()` or `TE_CoreManagerSetPluginEnabledByName()` from this thread cause data races (P0 blocker from technical audit).

---

## 4.3 Synchronization Primitives

| Primitive | Location | Protects | Access Pattern |
|---|---|---|---|
| `SRWLOCK config_lock` | `TE_CoreState` | `config_root` (cJSON tree) | Shared reads (plugins), Exclusive writes (reload) |
| `SRWLOCK` in state store | `state_store.c` | State hash map | Shared reads (query), Exclusive writes (publish) |
| `InterlockedCompareExchange` | `te_log_impl.c` | Ring buffer write position | Lock-free atomic CAS |
| `PostMessage` | IPC server → UI thread | State mutation serialization | Message queue serialization |
| `CreateTimerQueueTimer` | `fault_isolation.c` | Watchdog timeout | Timer callback on thread pool |

### Rules

- **Prefer `SRWLock`** over `CRITICAL_SECTION` (lighter weight, supports shared/exclusive).
- **Never hold a lock while calling a plugin callback** (deadlock risk if plugin tries to log or publish state).
- **Use `InterlockedCompareExchange` / `InterlockedIncrement`** for lock-free atomics (ring buffer, fault counters).
- **No busy waiting.** Use `WaitForSingleObject`, `WaitForMultipleObjects`, or event objects.

---

## 4.4 Race Condition Risks

### Risk 1: IPC Thread Data Race (P0 — MUST FIX)

**Scenario**: IPC server thread directly calls `TE_CoreManagerReloadConfig()` while the UI thread is dispatching events or calling plugin `Update()`.

**Impact**: Concurrent mutation of `config_root`, `plugins` array, subscription tables → memory corruption, access violations, Explorer crash.

**Fix**: Replace direct calls with `PostMessage(taskbar_hwnd, WM_TE_IPC_COMMAND, cmd_type, (LPARAM)payload_copy)`. All state mutations execute serially on the UI thread.

### Risk 2: Config Watcher Double-Fire

**Scenario**: Editor writes file in 2 steps (write temp + rename), generating 2 `ReadDirectoryChangesW` notifications within 100 ms.

**Impact**: Minor — redundant config reload. The debounce timer coalesces these.

**Mitigation**: Already handled by 100 ms debounce. Validated to work with VS Code, Notepad++, and vim.

### Risk 3: Plugin Disable During Event Dispatch

**Scenario**: Plugin A's event callback triggers `Disable()` on Plugin B (via IPC command arriving during dispatch).

**Impact**: Plugin B's subscriptions are removed while iterating the dispatch table.

**Mitigation**: IPC commands are marshaled to UI thread via `PostMessage`, so they execute AFTER the current dispatch loop completes (message queue serialization).

### Risk 4: Explorer Crash During Shutdown

**Scenario**: Explorer crashes while the Engine is executing its shutdown sequence (disabling plugins, freeing resources).

**Impact**: Resources may not be fully cleaned up. However, since Explorer crashed, the process is being torn down anyway — all process memory is freed by the OS.

**Mitigation**: Acceptable. The crash recovery thread in the Tray App detects this and re-injects into the new Explorer.

---

## 4.5 Shutdown Synchronization

```
Tray App                         Engine DLL (in Explorer)
  │                                   │
  ├─ Send TE_IPC_MSG_SHUTDOWN ──────► │
  │                                   ├─ PostMessage(WM_TE_IPC_COMMAND, SHUTDOWN)
  │                                   │   (queued behind any pending messages)
  │                                   ├─ UI thread processes SHUTDOWN:
  │                                   │   ├─ Disable() all plugins (reverse order)
  │                                   │   ├─ Shutdown() all plugins
  │                                   │   ├─ FreeLibrary all plugin DLLs
  │                                   │   ├─ Stop config watcher thread (signal + join)
  │                                   │   ├─ Stop IPC server thread (signal + join)
  │                                   │   ├─ Stop log flush thread (signal + join)
  │                                   │   ├─ RemoveWindowSubclass
  │                                   │   ├─ Free TE_CoreState
  │                                   │   └─ Send TE_IPC_MSG_SHUTDOWN_COMPLETE
  │                                   │
  ◄─ Receive SHUTDOWN_COMPLETE ──────┤
  ├─ UnhookWindowsHookEx             │
  │   (triggers DLL_PROCESS_DETACH)  │
  ├─ DllMain: cleanup globals        │
  └─ Tray App exits                   │
```

**Thread join order**: Config watcher → IPC server → Log flush (logger must be last to capture shutdown messages).

**Timeout**: If shutdown takes > 5 seconds, the Tray App force-unhooks and exits.

---

## 4.6 Resource Ownership — RAII Strategy

### C RAII Patterns

Since C has no destructors, use goto-cleanup pattern:

```c
HRESULT TE_SomeFunction(void) {
    HANDLE h = NULL;
    cJSON* json = NULL;
    HRESULT hr = E_FAIL;

    h = CreateFileW(...);
    if (h == INVALID_HANDLE_VALUE) goto cleanup;

    json = cJSON_Parse(buffer);
    if (!json) goto cleanup;

    hr = S_OK;

cleanup:
    if (json) cJSON_Delete(json);
    TE_SAFE_CLOSEHANDLE(h);
    return hr;
}
```

### SDK Cleanup Macros

```c
#define TE_SAFE_FREE(p)        do { if (p) { free(p); (p) = NULL; } } while(0)
#define TE_SAFE_RELEASE(p)     do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while(0)
#define TE_SAFE_CLOSEHANDLE(h) do { if ((h) && (h) != INVALID_HANDLE_VALUE) { CloseHandle(h); (h) = NULL; } } while(0)
```

### C++ RAII (COM/DComp)

```cpp
#include <wrl/client.h>  // Microsoft::WRL::ComPtr

ComPtr<IDCompositionDevice> device;
ComPtr<IDCompositionTarget> target;
ComPtr<IDCompositionVisual> root_visual;
// Automatically Release()'d when ComPtr goes out of scope
```

---

## 4.7 Windows HANDLE Management

| Handle Type | Acquisition | Release | Owner |
|---|---|---|---|
| `HHOOK` (CBT hook) | `SetWindowsHookExW` | `UnhookWindowsHookEx` | Tray App |
| `HMODULE` (Engine DLL) | `LoadLibraryW` | Auto-unloaded on unhook | Tray App |
| `HMODULE` (plugin DLLs) | `LoadLibraryW` | `FreeLibrary` | Plugin Loader |
| `HANDLE` (Explorer process) | `OpenProcess` | `CloseHandle` | Crash Recovery |
| `HANDLE` (Named Pipe) | `CreateNamedPipeW` | `CloseHandle` | IPC Server |
| `HANDLE` (Config dir) | `CreateFileW` | `CloseHandle` | Config Watcher |
| `HANDLE` (Log file) | `CreateFileW` | `CloseHandle` | Log Flush Thread |
| `HANDLE` (Timer) | `CreateTimerQueueTimer` | `DeleteTimerQueueTimer` | Fault Isolation |
| `HANDLE` (Thread) | `CreateThread` | Auto-closed on exit | Various |

---

## 4.8 GPU/Graphics Resource Lifetime

| Resource | Creation | Destruction | Owner |
|---|---|---|---|
| `IDCompositionDevice` | `DCompositionCreateDevice(NULL)` in `Enable()` | `Release()` in `Disable()` | IconHover plugin |
| `IDCompositionTarget` | `device->CreateTargetForHwnd(overlay)` | `Release()` in `Disable()` | IconHover plugin |
| `IDCompositionVisual` (root) | `device->CreateVisual()` | `Release()` in `Disable()` | IconHover plugin |
| `IDCompositionVisual` (per-icon) | Pre-allocated array in `Enable()` | `Release()` per visual in `Disable()` | IconHover plugin |
| `IDCompositionSurface` (per-icon) | Created on icon capture | `Release()` on cache invalidation | IconHover plugin |
| Overlay HWND | `CreateWindowExW` in `Enable()` | `DestroyWindow` in `Disable()` | IconHover plugin |
| `IUIAutomation*` | `CoCreateInstance` in `Enable()` | `Release()` in `Disable()` | IconHover plugin |

**Rule**: All DComp resources are created in `Enable()` and destroyed in `Disable()`. The `Disable()` contract requires FULL revert — no DComp artifacts must remain.

---

## 4.9 Failure Handling Strategy

### Plugin Failures

| Failure Mode | Detection | Response |
|---|---|---|
| Null pointer dereference | SEH `EXCEPTION_ACCESS_VIOLATION` | Disable plugin, log, tray notification |
| Stack overflow | SEH `EXCEPTION_STACK_OVERFLOW` | `_resetstkoflw()`, disable plugin |
| Timeout (>100 ms callback) | Watchdog timer | Increment fault counter, disable after 3 strikes |
| `Initialize()` returns failure | HRESULT check | Skip plugin, log warning |
| `Enable()` returns failure | HRESULT check | Mark plugin as failed, do not dispatch events |
| `Disable()` crashes | SEH | Log error, force-mark as disabled |

### Win32 API Failures

| API | Failure Check | Response |
|---|---|---|
| `FindWindowW("Shell_TrayWnd")` | Returns NULL | Abort Phase A, log error |
| `SetWindowSubclass` | Returns FALSE | Abort Phase A, log error |
| `LoadLibraryW` (plugin) | Returns NULL | Skip plugin, log `GetLastError()` |
| `CreateNamedPipeW` | Returns INVALID_HANDLE_VALUE | Log error, IPC disabled (non-fatal) |
| `ReadDirectoryChangesW` | Returns FALSE | Log error, config watching disabled (non-fatal) |
| `CoCreateInstance` (UIA) | Returns failure HRESULT | Disable IconHover, log error |
| `DCompositionCreateDevice` | Returns failure HRESULT | Disable IconHover, log error |
| `SystemParametersInfoW(SPI_SETWORKAREA)` | Returns FALSE | Log warning, continue |

### Configuration Failures

| Failure | Response |
|---|---|
| Config file not found | Copy `default_config.jsonc` to config location, use defaults |
| Config file malformed JSON | Log error, keep previous valid config |
| Plugin section missing | Use setting defaults from `SettingDescriptor` |
| Value out of range | Clamp to min/max from `SettingDescriptor`, log warning |
| Hot-reload parse failure | Log error, keep previous valid config |

### Recovery Behavior

**Core principle**: NEVER crash Explorer. Every failure must:
1. Log the error with context (function name, error code, affected plugin)
2. Disable the offending plugin gracefully
3. Show a tray balloon notification: "Plugin 'X' was disabled due to an error"
4. Continue running with remaining plugins

**Forbidden**: `exit()`, `abort()`, `TerminateProcess()` — these kill Explorer.

---

## 4.10 SEH Fault Isolation Details

### What SEH Catches (≈60% of failures)

- Null pointer dereference
- Access violation (read/write to unmapped memory)
- Divide by zero
- Stack overflow (with `_resetstkoflw()`)
- Illegal instruction
- Privileged instruction

### What SEH Cannot Catch (≈40% of failures)

- Heap corruption (double free, buffer overflow) — delayed effect, corrupts Explorer
- Deadlocks (plugin holds SRWLock while waiting on shell thread) — hangs message pump
- Memory leaks — gradual resource exhaustion
- COM exceptions crossing the C ABI boundary — may not propagate correctly
- Corruption of `TE_CoreState` — wild writes to engine memory

### Implementation Pattern

```c
HRESULT TE_FaultIsolatedCall(PluginEntry* plugin, const char* method_name,
                              HRESULT (*method)(void)) {
    __try {
        return method();
    } __except (TE_FaultFilter(GetExceptionInformation(), plugin->metadata->name)) {
        plugin->fault_count++;
        TE_LogWrite(TE_LOG_ERROR, "Plugin '%s' faulted in %s (code 0x%08X)",
            plugin->metadata->name, method_name, GetExceptionCode());

        if (plugin->fault_count >= 3) {
            TE_LogWrite(TE_LOG_ERROR, "Plugin '%s' disabled after 3 faults",
                plugin->metadata->name);
            /* Attempt graceful disable under SEH */
            __try { plugin->interface->Disable(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
            plugin->enabled = false;
        }
        return E_FAIL;
    }
}
```

**Honest assessment**: The "graceful fault isolation" promise is only partially deliverable. SEH handles acute crashes. Chronic corruption (heap, state, leaks) will manifest as mysterious Explorer instability. This is inherent to in-process DLL injection — there is no hardware-enforced sandbox for user-mode code.
