# 19 — Appendix & Reference Tables

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: Reference Specifications

---

## 1. Glossary of Terms

| Term | Definition |
|---|---|
| **ABI** | Application Binary Interface. The low-level data structure and calling convention contract between `EngineDLL.dll` and plugin DLLs. |
| **CBT Hook** | `WH_CBT` (Computer-Based Training) Windows message hook used to inject `EngineDLL.dll` into `explorer.exe`. |
| **DComp** | Microsoft DirectComposition. A GPU-accelerated visual composition engine for fluid, high-refresh transforms. |
| **JSONC** | JSON with Comments. The human-readable configuration file format used by TaskbarEngine. |
| **SEH** | Structured Exception Handling (`__try` / `__except`). Win32 hardware and software exception containment mechanism. |
| **Subclassing** | `SetWindowSubclass` (`comctl32.dll`). The standard, safe mechanism to intercept window messages on `Shell_TrayWnd`. |
| **UIA** | Microsoft UI Automation. Accessible element tree discovery API used to locate taskbar icon positions. |

---

## 2. IPC Message Protocol Reference

| Message ID | Enum Identifier | Direction | Payload Description |
|---|---|---|---|
| `1` | `TE_IPC_MSG_SHUTDOWN` | GUI/Tray $\rightarrow$ Engine | Request clean engine shutdown and un-subclassing. |
| `2` | `TE_IPC_MSG_SHUTDOWN_COMPLETE`| Engine $\rightarrow$ GUI/Tray | Acknowledgement that engine resources are released. |
| `3` | `TE_IPC_MSG_GET_PLUGIN_LIST` | GUI $\rightarrow$ Engine | Query list of discovered plugin names and status. |
| `4` | `TE_IPC_MSG_PLUGIN_LIST` | Engine $\rightarrow$ GUI | Tab-separated string: `<name>\t<enabled\|disabled>\n`. |
| `5` | `TE_IPC_MSG_ENABLE_PLUGIN` | GUI $\rightarrow$ Engine | Plugin name string to enable. |
| `6` | `TE_IPC_MSG_DISABLE_PLUGIN`| GUI $\rightarrow$ Engine | Plugin name string to disable. |
| `7` | `TE_IPC_MSG_RELOAD_CONFIG` | GUI $\rightarrow$ Engine | Request immediate disk configuration hot-reload. |
| `8` | `TE_IPC_MSG_STATUS` | Engine $\rightarrow$ GUI | Status response string (`"OK"`, `"ERROR"`, etc.). |
| `9` | `TE_IPC_MSG_GET_SETTINGS` | GUI $\rightarrow$ Engine | Query plugin setting descriptor schemas. |
| `10`| `TE_IPC_MSG_SETTINGS_RESPONSE` | Engine $\rightarrow$ GUI | JSON string containing setting descriptor tree. |
| `11`| `TE_IPC_MSG_GET_PERF_STATS` | GUI $\rightarrow$ Engine | Query live rendering performance telemetry. |
| `12`| `TE_IPC_MSG_PERF_STATS_RESPONSE` | Engine $\rightarrow$ GUI | JSON string: `{"fps":..., "avg_ms":..., "min_ms":..., "max_ms":...}`. |

---

## 3. Undocumented API Policy & Inventory

TaskbarEngine strictly follows the Windows API policy defined in `docs/design_decisions.md`:

| API | DLL | Purpose | Why No Documented Alternative Exists | Fallback / Isolation |
|---|---|---|---|---|
| `RtlGetVersion` | `ntdll.dll` | Accurate OS build number retrieval | Documented `GetVersionEx` is virtualized and locked to `10.0.19041` by OS compatibility manifests. | Isolated in `te_version.c`. Falls back to manifest version if unavailable. |

---

## 4. Documentation Index & Cross-References

- [00 — Project Overview](00_Project_Overview.md)
- [01 — System Architecture](01_System_Architecture.md)
- [02 — Project Structure](02_Project_Structure.md)
- [03 — Core Manager](03_Core_Manager.md)
- [04 — Plugin System](04_Plugin_System.md)
- [05 — Shared SDK](05_Shared_SDK.md)
- [06 — Settings GUI](06_GUI.md)
- [07 — Configuration Subsystem](07_Configuration.md)
- [08 — Event System & Inter-Plugin State](08_Event_System.md)
- [09 — Rendering & DirectComposition](09_Rendering.md)
- [10 — TaskbarResize Plugin](10_Taskbar_Resize.md)
- [11 — IconHover Magnification Engine](11_Icon_Hover.md)
- [12 — Performance Architecture](12_Performance.md)
- [13 — Benchmarking Suite](13_Benchmarking.md)
- [14 — Testing Strategy](14_Testing.md)
- [15 — API Reference](15_API_Reference.md)
- [16 — Build System & Toolchain](16_Build_System.md)
- [17 — Deployment & Packaging](17_Deployment.md)
- [18 — Future Features & Roadmap](18_Future_Features.md)
- [19 — Appendix & Reference Tables](19_Appendix.md)
