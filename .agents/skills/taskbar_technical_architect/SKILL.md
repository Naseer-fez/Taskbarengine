---
name: taskbar_technical_architect
description: Use this skill when you need deep technical details on TaskbarEngine's subsystems, APIs, performance targets, or thread models.
---
# TaskbarEngine Technical Architect

This skill provides references to the deep technical documentation for TaskbarEngine. 

**CRITICAL TOKEN-SAVING RULE:**
These documentation files are massive. DO NOT load them into context at once.
You MUST use `grep_search` to search for specific keywords (e.g., "Event Dispatch", "IPC Server", "DirectComposition") in the relevant file, identify the line numbers, and use `view_file` with `StartLine` and `EndLine` to read ONLY the paragraph/section you need.

**Available Documentation (in `d:/CODE/Utlities/Taskbar/implementations/`):**

- **`01_system_architecture.md`**: High-level design, component responsibilities, data flows, startup/shutdown sequences, and directory structure.
- **`02_technical_implementation.md`**: Win32 API usage (SetWindowsHookEx, SetWindowSubclass), DirectComposition pipeline, UIA discovery, JSONC hot-reload, and Named Pipe IPC structure.
- **`03_performance_optimization.md`**: Use when optimizing code or modifying hot paths (frame loops, event dispatch). Details CPU/GPU budgets and idle guarantees.
- **`04_concurrency_resources_stability.md`**: Use when handling threads, locks (SRWLock), memory management, crash recovery, or SEH fault isolation.

**Usage pattern:**
If you are implementing the IPC system, first `grep_search` for "IPC" in `02_technical_implementation.md`, find the line range, and read just that range using `view_file`. Do not read the rendering or performance docs until you strictly need them.
