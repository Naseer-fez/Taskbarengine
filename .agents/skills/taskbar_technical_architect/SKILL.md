---
name: taskbar_technical_architect
description: Use this skill when you need deep technical details on TaskbarEngine's subsystems, APIs, performance targets, or thread models.
---
# TaskbarEngine Technical Architect

This skill provides references to the deep technical documentation for TaskbarEngine. Do not load all of these files into context at once. Only read the file(s) relevant to your current task.

**Available Documentation (in `d:/CODE/Utlities/Taskbar/implementations/`):**

- **`01_system_architecture.md`**: Read this for the high-level design, component responsibilities, data flows, startup/shutdown sequences, and directory structure.
- **`02_technical_implementation.md`**: Read this for Win32 API usage (SetWindowsHookEx, SetWindowSubclass), DirectComposition pipeline, UIA discovery, JSONC hot-reload, and Named Pipe IPC structure.
- **`03_performance_optimization.md`**: Read this when optimizing code or modifying hot paths (frame loops, event dispatch). Details CPU/GPU budgets and idle guarantees.
- **`04_concurrency_resources_stability.md`**: Read this when handling threads, locks (SRWLock), memory management, crash recovery, or SEH fault isolation.

**Usage pattern:**
If you are implementing the IPC system, use `view_file` to read `02_technical_implementation.md` and `04_concurrency_resources_stability.md`. Do not read the rendering or performance docs until you need them.
