# Original User Request

## 2026-09-11T10:33:08Z

# Teamwork Project Prompt — Draft

> Status: Launched.
> Goal: Craft prompt → get user approval → delegate to teamwork_preview
> Requested team: Full team

Use a full team of agents to refactor the TaskbarEngine `icon_hover` module to eliminate duplicate taskbar background artifacts and achieve butter-smooth, 60–144Hz VSync-locked macOS Dock-style animation physics.

Working directory: d:\CODE\Utlities\Taskbar
Integrity mode: development

## Requirements

### R1. Pure Transparent Icon Extraction
- Extract true 32-bit ARGB transparent icons (via `SHGetFileInfoW`, `SHGetImageList(SHIL_JUMBO)`, `WM_GETICON`, or UIA `ControlType_Image` child element narrowing).
- Never capture rectangular taskbar background surfaces with `0xFF` opaque masks. If fallback screen capture is absolutely necessary, subtract the background color to compute an alpha mask.

### R2. Selective / Differential Rendering
- Only display icons in the DirectComposition overlay visual tree that are actively magnifying or settling (`scale > 1.001f`).
- Icons at rest (`scale = 1.0`) must have opacity `0.0f` or be detached from the root visual.
- Guarantee sub-pixel positioning with bottom edges anchored to the taskbar baseline and continuous tangent continuity for horizontal displacement.

### R3. VSync-Locked Render Pump & Animation Physics
- Implement a display-synchronized animation pump (`DwmFlush()` or `IDXGIOutput::WaitForVBlank`) on a dedicated high-priority animation worker thread.
- Replace linear lerping with a 2nd-order critically damped spring system (F = -k*x - c*v) or cubic ease-out velocity integrator.
- Apply low-pass exponential smoothing to incoming cursor coordinates.

### R4. Verification & Clean-up
- Code must compile with MSVC (`/W4 /WX`) and MinGW (`-Wall -Wextra -Werror`).
- Ensure 0% CPU consumption when idle and clean reclamation of all DirectComposition surfaces and GDI handles upon exit.

## Acceptance Criteria

### Execution & Tests
- [ ] Code compiles cleanly under the strict warning levels for both MSVC and MinGW.
- [ ] Catch2 unit tests pass, explicitly verifying spring settling, alpha isolation, and VSync pacing.
- [ ] Google Benchmarks confirm magnification compute time is under 500 ns.
- [ ] Process CPU usage drops to 0% when the cursor is off the taskbar and animations have settled.
- [ ] No GDI or COM object leaks occur after repeated hover cycles.

## 2026-09-11T10:37:33Z

# Teamwork Project Prompt — Draft

> Status: Launched
> Goal: Craft prompt → get user approval → delegate to teamwork_preview
> Requested team: The full team

Implement a native `taskbar_transparency` plugin module in the TaskbarEngine codebase that provides full taskbar transparency, acrylic blur, and custom opacity controls across Windows 10 and Windows 11.

Working directory: d:/CODE/Utlities/Taskbar
Integrity mode: development

## Requirements

### R1. Plugin Implementation
Create `taskbar_transparency.c`, headers, and `CMakeLists.txt` in `Modules/taskbar_transparency/`. Implement the `PluginInterface` lifecycle (`Initialize`, `Enable`, `Disable`, `Update`, `Shutdown`). Resolve `SetWindowCompositionAttribute` from `user32.dll`.

### R2. Visual Modes
Support four distinct modes via `SetWindowCompositionAttribute`:
1. Clear / Transparent (`ACCENT_ENABLE_TRANSPARENTGRADIENT` with alpha = 0).
2. Blur Behind (`ACCENT_ENABLE_BLURBEHIND`).
3. Acrylic (`ACCENT_ENABLE_ACRYLICBLURBEHIND`).
4. Color Tint / Opaque.

### R3. Multi-Monitor & Shell Stability
Enumerate and apply transparency to all active taskbar instances (`Shell_TrayWnd` and `Shell_SecondaryTrayWnd`). Handle `TE_EVENT_DISPLAY_CHANGED` and `TE_EVENT_DPI_CHANGED` to re-apply policy if Explorer resets it.

### R4. Configuration & Live Hot-Reloading
Add `"taskbar_transparency"` section to `Config/default_config.jsonc`. Subscribe to `TE_EVENT_CONFIG_CHANGED` to update transparency instantly without restart.

### R5. Build Integration & Verification
Register in root `CMakeLists.txt`, add post-build copy rules, and verify compilation with MSVC (`/W4 /WX`) and MinGW (`-Wall -Wextra -Werror`). Add unit tests in `Tests/`.

## Acceptance Criteria

### Taskbar Visuals
- [ ] Starting TaskbarEngine with `taskbar_transparency` enabled makes the taskbar background 100% transparent or frosted blur based on config.
- [ ] Exiting TaskbarEngine immediately restores the native Windows taskbar background.

### Performance
- [ ] Consumes 0.0% CPU when idle.

### Build Quality
- [ ] Dual-toolchain compilation passes cleanly with zero warnings and zero errors.

## 2026-09-11T10:59:33Z

# Orchestrator Resumption Directive

**CRITICAL CONTEXT:** You are resuming the orchestration of the TaskbarEngine `icon_hover` module refactor. Your previous instance was terminated due to a quota limit error during Phase 2 (Milestone 1 Verification).

**IMMEDIATE ACTIONS:**
1. Read `PROJECT.md` and `.agents/orchestrator/progress.md` to re-orient yourself with the exact project state and feature inventory.
2. According to `progress.md`, Worker M1 completed `Modules/icon_hover/icon_capture.cpp` (Pure Transparent Icon Extraction), and you dispatched 5 verification agents (reviewer_m1_1, reviewer_m1_2, challenger_m1_1, challenger_m1_2, auditor_m1).
3. Check their output directories (e.g., `.agents/teamwork_preview_reviewer_m1_1/handoff.md`). If they failed to complete due to the crash, you must re-dispatch them or perform the verification yourself.
4. Once Milestone 1 is verified, proceed with the roadmap:
   - Phase 3: Milestone 2 — Selective / Differential Rendering
   - Phase 4: Milestone 3 — VSync-Locked Animation Pump & Physics
   - Phase 5: Milestone 4 — Integration & Verification
   - Phase 6: Final Hardening & Audit
5. Update `.agents/orchestrator/progress.md` as you proceed.

--- ORIGINAL USER PROMPT ---
Use a full team of agents to refactor the TaskbarEngine `icon_hover` module to eliminate duplicate taskbar background artifacts and achieve butter-smooth, 60–144Hz VSync-locked macOS Dock-style animation physics.

Working directory: d:\CODE\Utlities\Taskbar
Integrity mode: development

## Requirements

### R1. Pure Transparent Icon Extraction
- Extract true 32-bit ARGB transparent icons (via `SHGetFileInfoW`, `SHGetImageList(SHIL_JUMBO)`, `WM_GETICON`, or UIA `ControlType_Image` child element narrowing).
- Never capture rectangular taskbar background surfaces with `0xFF` opaque masks. If fallback screen capture is absolutely necessary, subtract the background color to compute an alpha mask.

### R2. Selective / Differential Rendering
- Only display icons in the DirectComposition overlay visual tree that are actively magnifying or settling (`scale > 1.001f`).
- Icons at rest (`scale = 1.0`) must have opacity `0.0f` or be detached from the root visual.
- Guarantee sub-pixel positioning with bottom edges anchored to the taskbar baseline and continuous tangent continuity for horizontal displacement.

### R3. VSync-Locked Render Pump & Animation Physics
- Implement a display-synchronized animation pump (`DwmFlush()` or `IDXGIOutput::WaitForVBlank`) on a dedicated high-priority animation worker thread.
- Replace linear lerping with a 2nd-order critically damped spring system (F = -k*x - c*v) or cubic ease-out velocity integrator.
- Apply low-pass exponential smoothing to incoming cursor coordinates.

### R4. Verification & Clean-up
- Code must compile with MSVC (`/W4 /WX`) and MinGW (`-Wall -Wextra -Werror`).
- Ensure 0% CPU consumption when idle and clean reclamation of all DirectComposition surfaces and GDI handles upon exit.

## Acceptance Criteria

### Execution & Tests
- [ ] Code compiles cleanly under the strict warning levels for both MSVC and MinGW.
- [ ] Catch2 unit tests pass, explicitly verifying spring settling, alpha isolation, and VSync pacing.
- [ ] Google Benchmarks confirm magnification compute time is under 500 ns.
- [ ] Process CPU usage drops to 0% when the cursor is off the taskbar and animations have settled.
- [ ] No GDI or COM object leaks occur after repeated hover cycles.

## 2026-09-11T14:35:38Z

# Orchestrator Resumption Directive (Quota Recovery)

**CRITICAL CONTEXT:** You are resuming the orchestration of the TaskbarEngine `icon_hover` module refactor. Your previous instance was terminated due to a quota limit error during Phase 2 (Milestone 1, Iteration 2). The API quota has now reset and you may resume at full speed.

**IMMEDIATE ACTIONS:**
1. Read `.agents/orchestrator/progress.md` and `PROJECT.md` to re-orient yourself.
2. According to `progress.md`, `worker_m1_fix` was actively implementing the remediation plan (IImageList GUID correction, leak-free GDI handle lifecycle, and real Catch2 live test integration).
3. However, `worker_m1_fix` failed to complete due to the quota crash (`.agents/worker_m1_fix/handoff.md` does NOT exist).
4. You must re-dispatch the worker (or assume the task) to complete the Milestone 1 Iteration 2 fixes to `Modules/icon_hover/icon_capture.cpp`, `Tests/CMakeLists.txt`, and `Tests/test_icon_hover.cpp`.
5. Once the implementation is definitively finished, deploy the full verification swarm for Gate 2.
6. Upon Gate 2 PASS, proceed with the original roadmap:
   - Phase 3: Milestone 2 — Selective / Differential Rendering
   - Phase 4: Milestone 3 — VSync-Locked Animation Pump & Physics
   - Phase 5: Milestone 4 — Integration & Verification
   - Phase 6: Final Hardening & Audit
7. Update `.agents/orchestrator/progress.md` as you proceed.

--- ORIGINAL USER PROMPT ---
Use a full team of agents to refactor the TaskbarEngine `icon_hover` module to eliminate duplicate taskbar background artifacts and achieve butter-smooth, 60–144Hz VSync-locked macOS Dock-style animation physics.

Working directory: d:\CODE\Utlities\Taskbar
Integrity mode: development

## Requirements

### R1. Pure Transparent Icon Extraction
- Extract true 32-bit ARGB transparent icons (via `SHGetFileInfoW`, `SHGetImageList(SHIL_JUMBO)`, `WM_GETICON`, or UIA `ControlType_Image` child element narrowing).
- Never capture rectangular taskbar background surfaces with `0xFF` opaque masks. If fallback screen capture is absolutely necessary, subtract the background color to compute an alpha mask.

### R2. Selective / Differential Rendering
- Only display icons in the DirectComposition overlay visual tree that are actively magnifying or settling (`scale > 1.001f`).
- Icons at rest (`scale = 1.0`) must have opacity `0.0f` or be detached from the root visual.
- Guarantee sub-pixel positioning with bottom edges anchored to the taskbar baseline and continuous tangent continuity for horizontal displacement.

### R3. VSync-Locked Render Pump & Animation Physics
- Implement a display-synchronized animation pump (`DwmFlush()` or `IDXGIOutput::WaitForVBlank`) on a dedicated high-priority animation worker thread.
- Replace linear lerping with a 2nd-order critically damped spring system (F = -k*x - c*v) or cubic ease-out velocity integrator.
- Apply low-pass exponential smoothing to incoming cursor coordinates.

### R4. Verification & Clean-up
- Code must compile with MSVC (`/W4 /WX`) and MinGW (`-Wall -Wextra -Werror`).
- Ensure 0% CPU consumption when idle and clean reclamation of all DirectComposition surfaces and GDI handles upon exit.

## Acceptance Criteria

### Execution & Tests
- [ ] Code compiles cleanly under the strict warning levels for both MSVC and MinGW.
- [ ] Catch2 unit tests pass, explicitly verifying spring settling, alpha isolation, and VSync pacing.
- [ ] Google Benchmarks confirm magnification compute time is under 500 ns.
- [ ] Process CPU usage drops to 0% when the cursor is off the taskbar and animations have settled.
- [ ] No GDI or COM object leaks occur after repeated hover cycles.

## 2026-09-12T05:49:14Z

# Server Restart Recovery Protocol

The server experienced an overnight restart, which automatically halted all background tasks, Sentinel crons, and subagents (including active workers). The API quotas have completely reset, and it is a new day (2026-09-12).

Please resume the orchestration exactly where you left off.
According to `d:\CODE\Utlities\Taskbar\.agents\orchestrator\progress.md`, you were in the middle of Phase 3 (Milestone 2) and were dispatching `worker_m2` to implement Features F6, F7, and F8.

IMMEDIATE ACTIONS:
1. Re-activate your background Sentinel monitoring crons.
2. Check if `worker_m2` ever completed its work before the server crash by looking for `.agents/worker_m2/handoff.md`. If it did not, re-dispatch it.
3. Once `worker_m2` is complete, deploy the Gate 3 verification swarm for Milestone 2.
4. Continue through Phase 4 (Milestone 3), Phase 5 (Milestone 4), and Phase 6 (Final Audit).
