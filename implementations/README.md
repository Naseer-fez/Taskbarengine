# TaskbarEngine — Technical Reconstruction Specification

> **Purpose**: This document set is the master reconstruction reference for rebuilding TaskbarEngine from an empty repository.
>
> **Source of Truth**: All content is derived from the project's design interview, `docs/design_decisions.md`, `docs/phases/`, and `docs/technical_audit.md`.
>
> **Target Audience**: AI coding agents and senior engineers performing phase-by-phase implementation.

---

## Document Index

| Part | File | Contents |
|---|---|---|
| 1 | [01_system_architecture.md](01_system_architecture.md) | Project purpose, subsystems, data flow, lifecycle, source tree |
| 2 | [02_technical_implementation.md](02_technical_implementation.md) | Win32 APIs, window management, taskbar integration, rendering, IPC, configuration |
| 3 | [03_performance_optimization.md](03_performance_optimization.md) | CPU/GPU hot paths, memory strategy, frame timing, idle guarantees |
| 4 | [04_concurrency_resources_stability.md](04_concurrency_resources_stability.md) | Threading model, synchronization, resource ownership, failure handling |
| 5 | [05_constraints_design_decisions.md](05_constraints_design_decisions.md) | MUST HAVE / MUST NOT / PERFORMANCE / ARCHITECTURE / PLATFORM / DEPENDENCIES |
| 6 | [06_reconstruction_phases.md](06_reconstruction_phases.md) | 5-phase dependency-aware implementation roadmap |
| 7 | [07_coding_agent_rules.md](07_coding_agent_rules.md) | Implementation rules, coding standards, validation requirements |

---

## Reading Order

1. Read **Part 5** (Constraints) first to understand hard boundaries.
2. Read **Part 1** (Architecture) for the full system picture.
3. Read **Part 2** (Implementation) for API-level technical details.
4. Read **Part 3** (Performance) and **Part 4** (Concurrency) for optimization and safety.
5. Read **Part 6** (Phases) as the implementation roadmap.
6. Read **Part 7** (Agent Rules) before writing any code.

---

## Key Architectural Facts (Quick Reference)

| Fact | Value |
|---|---|
| Process model | Two-process: Tray App (EXE) + Engine DLL (in Explorer) |
| Injection | `SetWindowsHookEx(WH_CBT)` → DLL into `explorer.exe` |
| Initialization | Deferred via `PostMessage` (outside loader lock) |
| Subclassing | `SetWindowSubclass` on `Shell_TrayWnd` (comctl32) |
| Plugin ABI | Pure C17 vtable struct, `extern "C"`, frozen layout |
| Config format | JSONC parsed by vendored `cJSON` (~1000 LOC, pure C) |
| Config location | `%LOCALAPPDATA%\TaskbarEngine\config.jsonc` |
| IPC transport | Named Pipes, binary protocol, restricted ACL |
| Event dispatch | Synchronous function-pointer callbacks |
| Rendering | DirectComposition overlay (GPU-composited) |
| Animation | Vsync-locked frame loop, self-canceling on mouse leave |
| Languages | C17 default; C++17 only for COM/DComp/UIA/WinUI 3 |
| Build system | CMake + Ninja, dual toolchain (MSVC Release + Clang-cl Debug) |
| Testing | Catch2 (unit), Google Benchmark (perf) |
| Min Windows | Windows 11 22H2 (build 22621)+ |
| Distribution | Portable ZIP, MIT license |
