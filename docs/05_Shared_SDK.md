# 05 — Shared SDK

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: `SDK/` (`te_sdk.lib`)

---

## 1. Overview & Scope

The **TaskbarEngine Shared SDK** is a lightweight, pure C17 static library (`te_sdk`) providing shared data structures, math functions, IPC utilities, logging, JSONC parsing, and Windows build detection helpers across the Core Engine, Plugins, Tray Host, and Test Suites.

```
SDK/
├── include/sdk/
│   ├── te_types.h          # Core type definitions, HRESULT macros, TE_API_VERSION
│   ├── te_plugin.h         # Plugin ABI vtable and context contracts
│   ├── te_events.h         # Engine event IDs and event payload structs
│   ├── te_ipc.h            # Named pipe framing, headers, and serialization
│   ├── te_jsonc.h          # JSONC parsing wrapper for cJSON
│   ├── te_log.h            # Multi-level asynchronous logging interface
│   ├── te_version.h        # OS build detection via RtlGetVersion
│   ├── te_easing.h         # Easing curves (Gaussian, Cubic, Bezier, etc.)
│   └── te_dpi.h            # DPI scale factor calculation utilities
└── src/                    # SDK implementations
```

---

## 2. Core Modules & Public Interfaces

### A. Core Types & Macros (`te_types.h`)
- **API Version**: `#define TE_API_VERSION 2`
- **Result Macros**:
  ```c
  #define TE_SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
  #define TE_FAILED(hr)    (((HRESULT)(hr)) < 0)
  ```
- **Safe Cleanup**:
  ```c
  #define TE_SAFE_FREE(p)        do { if (p) { free(p); (p) = NULL; } } while(0)
  #define TE_SAFE_RELEASE(p)     do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while(0)
  #define TE_SAFE_CLOSEHANDLE(h) do { if ((h) && (h) != INVALID_HANDLE_VALUE) { CloseHandle(h); (h) = NULL; } } while(0)
  ```

### B. Asynchronous Logging Subsystem (`te_log.h`)
- **Log Levels**: `TE_LOG_DEBUG`, `TE_LOG_INFO`, `TE_LOG_WARN`, `TE_LOG_ERROR`.
- **Thread Safety**: Uses an in-memory lock-free SRWLock ring buffer. Calls to `TE_LogWrite()` complete in < 5 µs without blocking the UI thread.
- **Background Flusher**: Dedicated background thread periodically drains entries to `%LOCALAPPDATA%\TaskbarEngine\TaskbarEngine.log`.
- **Debug Logs**: Stripped from Release builds via `#ifdef TE_DEBUG`.

### C. JSONC Parser Wrapper (`te_jsonc.h`)
- Wraps vendored ANSI C `cJSON`.
- Automatically strips single-line (`//`) and multi-line (`/* */`) comments before parsing.
- API:
  ```c
  HRESULT TE_JsoncParse(const wchar_t* file_path, cJSON** out_root);
  HRESULT TE_JsoncParseString(const char* jsonc_text, cJSON** out_root);
  ```

### D. Named Pipe IPC Serialization (`te_ipc.h`)
- **Pipe Name**: `\\.\pipe\TaskbarEngine`
- **Header Structure**:
  ```c
  typedef struct TE_IpcHeader {
      uint32_t magic;           /* 0x54454950 ('TEIP') */
      uint32_t version;         /* 1 */
      uint32_t type;            /* TE_IpcMsgType */
      uint32_t payload_length;  /* Length of following data (max 64 KB) */
  } TE_IpcHeader;
  ```
- **Serialization Utilities**: `TE_IpcSerialize`, `TE_IpcValidateHeader`, `TE_IpcReadExact`.

### E. Easing & Animation Curves (`te_easing.h`)
Optimized mathematical curves computed without dynamic allocations:
- **Gaussian**: \(f(x) = \exp\left(-\frac{x^2}{2\sigma^2}\right)\)
- **Cubic Ease-Out**: \(f(t) = 1 - (1 - t)^3\)
- **Cosine Wave**: Smooth symmetric windowing
- **Linear**: Direct interpolation

### F. Windows Version Detection (`te_version.h`)
- Uses `RtlGetVersion` (from `ntdll.dll`) to bypass OS manifest application version virtualization.
- Returns accurate build numbers (e.g. 22621 for 22H2, 22631 for 23H2, 26100 for 24H2).
- Validates plugin build ranges (`min_build` $\le$ `current_build` $\le$ `max_tested_build`).
