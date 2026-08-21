# 15 — API Reference

> **Source of Truth**: `SDK/include/sdk/te_plugin.h` & `Doxyfile`  
> **Component**: Public SDK & Core APIs

---

## 1. Doxygen HTML Documentation

Complete function-level documentation with parameter definitions, return values, thread safety annotations, and call graphs is generated via **Doxygen**:

```powershell
# Generate HTML API documentation in Docs/api/
doxygen Doxyfile
```

The generated documentation entry point is located at:
- **`Docs/api/html/index.html`**

---

## 2. Core Plugin ABI Reference (`te_plugin.h`)

### A. Entry Point Export
```c
/**
 * @brief Entry point exported by every TaskbarEngine plugin DLL.
 */
TE_EXPORT const PluginInterface* GetPluginInterface(void);
```

### B. `PluginInterface` VTable Methods
| Method Signature | Thread Safety | Description |
|---|---|---|
| `HRESULT (*Initialize)(const PluginContext* ctx)` | UI Thread | Initial setup, buffer allocations, and context caching. |
| `HRESULT (*Enable)(void)` | UI Thread | Activate visual hooks, subscribe to events, and start rendering. |
| `HRESULT (*Disable)(void)` | UI Thread | Revert all taskbar modifications and restore native geometry. |
| `HRESULT (*Update)(float deltaTime)` | UI Thread | Animation tick callback (only while animations are active). |
| `HRESULT (*Shutdown)(void)` | UI Thread | Release all allocated resources and COM pointers. |
| `const PluginMetadata* (*GetMetadata)(void)` | Any Thread | Return static metadata struct (name, version, author, priority).|
| `const PluginSettings* (*GetSettings)(void)` | Any Thread | Return static array of setting descriptors for GUI generation. |

### C. `PluginContext` Services (Core $\rightarrow$ Plugin)
| Function Pointer | Signature | Description |
|---|---|---|
| `subscribe` | `HRESULT (*)(uint32_t event, EventCallbackFunc cb, void* udata)` | Subscribe to an engine event. |
| `unsubscribe` | `HRESULT (*)(uint32_t event, EventCallbackFunc cb)` | Unsubscribe from an engine event. |
| `subscribe_message` | `HRESULT (*)(UINT win_msg)` | Hook a raw Win32 message on `Shell_TrayWnd`. |
| `unsubscribe_message`| `HRESULT (*)(UINT win_msg)` | Unhook a raw Win32 message on `Shell_TrayWnd`. |
| `register_timer` | `HRESULT (*)(uint32_t ms, BOOL recurring, TE_TimerCallback cb, void* udata)` | Register a UI-thread timer. |
| `cancel_timer` | `HRESULT (*)(TE_TimerCallback cb)` | Cancel an active UI-thread timer. |
| `publish_state` | `HRESULT (*)(const char* key, const StateValue* val)` | Publish a value to the shared blackboard.|
| `query_state` | `HRESULT (*)(const char* key, StateValue* out_val)` | Query a value from the shared blackboard. |
| `log` | `void (*)(int level, const char* fmt, ...)` | Thread-safe logging function. |

---

## 3. Named Pipe IPC API (`te_ipc.h`)

### Message Types (`TE_IpcMsgType`)
- `TE_IPC_MSG_SHUTDOWN` (1) / `TE_IPC_MSG_SHUTDOWN_COMPLETE` (2)
- `TE_IPC_MSG_GET_PLUGIN_LIST` (3) / `TE_IPC_MSG_PLUGIN_LIST` (4)
- `TE_IPC_MSG_ENABLE_PLUGIN` (5) / `TE_IPC_MSG_DISABLE_PLUGIN` (6)
- `TE_IPC_MSG_RELOAD_CONFIG` (7)
- `TE_IPC_MSG_STATUS` (8)
- `TE_IPC_MSG_GET_SETTINGS` (9) / `TE_IPC_MSG_SETTINGS_RESPONSE` (10)
- `TE_IPC_MSG_GET_PERF_STATS` (11) / `TE_IPC_MSG_PERF_STATS_RESPONSE` (12)
