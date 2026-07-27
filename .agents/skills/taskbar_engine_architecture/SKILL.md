---
name: TaskbarEngine Architecture Context
description: >
  Architectural decisions, design constraints, and implementation guidelines
  for the TaskbarEngine project. Reference this skill when implementing any
  component of the TaskbarEngine system.
---

# TaskbarEngine Architecture Context

This skill provides the architectural context for the TaskbarEngine project.

## Source of Truth

The authoritative design document is: `docs/design_decisions.md`
The implementation roadmap is in: `docs/phases/`

Always read these before implementing any component. They supersede
`docs/00_Project_Overview.md` wherever there are conflicts.

## Key Design Principles (from interview)

1. **Pure C plugin ABI** — No COM, no C++ at the ABI boundary. Vtable struct with function pointers.
2. **JSONC config** — Parsed by vendored `cJSON` (single C file). NOT TOML, NOT toml++.
3. **Two-process model** — Tray App (EXE) + Engine DLL (in Explorer). NOT three-process.
4. **Deferred init** — `DllMain` only guards + posts a message. Real init runs outside the loader lock. NEVER call `LoadLibrary` or `CreateThread` in `DllMain`.
5. **SetWindowSubclass** — Use the safe `comctl32` API, not `SetWindowLongPtr(GWLP_WNDPROC)`.
6. **WH_CBT hook** for injection. NOT WH_SHELL, NOT AppInit_DLLs.
7. **No signature verification** on plugins. Trust the user.
8. **GUI writes config, Engine only reads**. No file locking.
9. **Synchronous event dispatch** — Function pointer callbacks, no async queue.
10. **SEH + watchdog** around all plugin callbacks.

## User Preferences

- Prefers **simpler/lighter** solutions when both options are viable.
- Prefers **pure C** over C++ wherever possible.
- Prefers **vendored single-file libraries** over large frameworks.
- Values **zero external runtime dependencies**.
- Stores projects on **D: drive** per global rule.
- Config at `%LOCALAPPDATA%\TaskbarEngine\config.jsonc`.

## Language Rules

- **C17** for all C code. **C++17** only where C is insufficient.
- C++ allowed for: DirectComposition, UIA, COM, WinUI 3 GUI.
- Rust: gated behind >5% benchmark improvement.

## Build

- CMake + Ninja. Dual toolchain: MSVC (Release) + Clang-cl (Debug/Sanitizers).
- Catch2 for tests. Google Benchmark for micro-benchmarks.
- Azure DevOps Pipelines for CI.

## Implementation Phases

| Phase | Name | Complexity |
|---|---|---|
| 1 | Project Foundation + Injection Proof | Medium |
| 2 | Core Manager + Plugin Lifecycle | High |
| 3 | TaskbarResize + IPC + Crash Recovery | High |
| 4 | IconHover + Animation Engine | Very High |
| 5 | GUI + Benchmarks + CI + Release | Medium |

See `docs/phases/` for detailed phase specifications.
