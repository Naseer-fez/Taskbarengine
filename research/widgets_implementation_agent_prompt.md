# Implementation brief: make TaskbarEngine Widgets reliable and extensible

You are the implementation agent working in this repository:

`D:\CODE\Utlities\Taskbar`

## Objective

Finish the Widgets feature so that the built-in widgets work reliably and users can add custom widgets through a clear, supported extension path. Implement the necessary changes in this codebase (including replacing whole files or changing APIs when justified), rather than stopping at a design proposal. Preserve existing behavior outside Widgets unless a change is necessary and documented.

The outcome should let a user:

1. Enable, disable, configure, and reorder the built-in clock, system monitor, and media widgets from Settings.
2. Add an additional custom widget without editing hard-coded GUI lists or rebuilding the engine. Define exactly how a developer packages/installs one, what API it implements, how it is discovered, and how the UI lists/configures it.
3. See the widget visibly rendered in the intended taskbar surface, interact with it, persist settings/order, and recover from Explorer/taskbar and graphics lifecycle changes.

Do not claim success based only on compilation, DLL loading, a successful Win32/COM call, or unit tests. Acceptance requires runtime evidence that the widget is visible and interactive.

## Start here: instructions and working-tree protection

- Read all applicable `AGENTS.md` and project instructions, including instructions above the repository root. The prior research sweep did not find a root `AGENTS.md`; verify that is still true.
- Read `research/widgets_reliability_research.md` in full. It is the repository-specific research record and includes Microsoft documentation links, source findings, risks, and a phased plan.
- Inspect the current `git status` and diffs before editing. This checkout already had many unrelated modified and untracked files during research, including App/Core/Modules/config/build/test/docs files and `Modules/widget_host/`. Treat those as user work: do not revert, overwrite, reformat, or “clean up” unrelated modifications. Re-read each target file as it exists now; do not assume the research report reflects the latest edits.
- Do not perform broad test-suite runs. Use focused build/runtime checks that directly establish the requested feature. If runtime execution is unavailable, state what was and was not verified and leave precise manual steps.
- Do not inject new code into Explorer or expand undocumented taskbar internals without first establishing whether the current route is a best-effort compatibility mode or a supported product requirement. Avoid any operation that risks Explorer or user settings.

## Repository map

Trace and change the actual data path; do not implement against a guessed architecture.

- `Modules/widget_host/widget_host.cpp` — plugin registration, settings descriptors, config event subscription.
- `Modules/widget_host/widget_host_runtime.cpp` — DLL discovery/loading, widget lifecycle, layout, Direct2D/DirectComposition, input, DPI/display events, configuration reload, cleanup.
- `Modules/widget_host/widget_host_model.cpp` / `.h` — widget order/layout and hit-test model.
- `Modules/widget_host/widgets/clock_widget.cpp`, `sysmon_widget.cpp`, `media_widget.cpp`, `widget_draw.h` — built-in widget implementations/helpers.
- `SDK/include/sdk/te_widget.h` — current custom widget ABI.
- `Modules/widget_host/CMakeLists.txt`, root `CMakeLists.txt` — targets and widget DLL output.
- `Modules/icon_hover/dcomp_overlay.h` and its implementation — shared DirectComposition resource bridge currently consumed by widget host.
- `Core/src/plugin_loader.c`, `Core/src/core_manager.c`, taskbar lifecycle/subclass sources, and IPC/config event definitions — plugin ordering, taskbar lifetime and settings propagation.
- `App/src/gui_main.cpp`, `App/src/settings_page.cpp`, `App/src/config_io.cpp/.h`, `App/src/gui_ipc_client.cpp` — navigation, Settings controls, JSON persistence, reload signaling.
- `Config/default_config.jsonc` — packaged default schema/data.
- `Scripts/build.ps1`, `deploy.ps1`, `package.ps1`, installer files — deployment layout and dependencies.
- `Tests/test_widget_host.cpp`, `Tests/CMakeLists.txt`, other focused config/IPC tests — existing narrow verification coverage.
- `Logs/taskbarengine.log` — old checked-in log; it contains no widget-host runtime diagnostics, so it cannot establish current feature behavior.

## What research found in the current implementation

Verify each item against the current working tree before acting:

- GUI has a Widgets navigation entry tagged `widget_host`; Settings has a hard-coded three-widget ordering/toggle UI and flat setting descriptors. GUI saves plugin `enabled` under `plugins.widget_host`, while other controls are mapped under root `widgets`.
- `widget_host_runtime.cpp` derives a `Widgets` directory relative to its DLL, enumerates `*.dll`, calls `LoadLibraryW`, resolves `GetWidgetInterface`, checks minimal callbacks/metadata, and loads built-in DLLs. This has no robust identity/version/compatibility contract and arbitrary native DLLs execute in-process.
- Widget activation requires `GetModuleHandleW(L"icon_hover.dll")` to succeed and all five private `TE_DCompWidget*` exports to be found. The widget host acquires DirectComposition resources shared through that module; it does not load the dependency itself. Failure to resolve/acquire leaves widgets inactive.
- Runtime stores a single taskbar HWND, subclasses it, renders through a one-second timer, and has no evident taskbar HWND reacquisition path after Explorer restarts. It handles some DPI/display/window-position messages and checks an overlay graphics generation, but these are not equivalent to full Explorer/taskbar lifecycle recovery or multi-monitor support.
- All plugin callbacks run in the same process. `__try/__except` guards do not isolate a hang, process termination, heap corruption, ABI mismatch, or arbitrary native side effects.
- There may be a layout type mismatch: default config uses an array for `widgets.layout`, while GUI fallback and parser may expect a comma-delimited string. Verify `widget_host_model.cpp` and config migration behavior before deciding. Pick one canonical representation and migrate old data losslessly.
- Config change handling may filter for `plugin_name == "widgets"` even though the plugin is named `widget_host`; inspect the config event producer and establish actual values before changing the filter.
- Existing logs offer no runtime evidence. Add useful structured diagnostics before guessing at the observed failure.

These are hypotheses/evidence from the research pass, not permission to assume that every item is the current bug. Use targeted inspection and runtime signals to confirm.

## Microsoft platform distinction and design constraints

Windows “Widgets” APIs are not APIs for embedding custom content in this application's taskbar.

Official Microsoft documentation:

- Windows Widgets overview: https://learn.microsoft.com/en-us/windows/apps/develop/widgets/
- Provider lifecycle and API availability: https://learn.microsoft.com/en-us/windows/apps/develop/widgets/widget-providers
- Win32 provider package manifest: https://learn.microsoft.com/en-us/windows/apps/develop/widgets/widget-provider-manifest
- C++/WinRT Win32 widget provider walkthrough: https://learn.microsoft.com/en-us/windows/apps/develop/widgets/implement-widget-provider-win32
- Windows App SDK support overview: https://learn.microsoft.com/en-us/windows/apps/get-started/windows-developer-faq
- Desktop windowing overview: https://learn.microsoft.com/en-us/windows/apps/develop/ui/windowing-overview
- DirectComposition: https://learn.microsoft.com/en-us/windows/win32/directcomp/directcomposition-portal

Microsoft's Widgets provider APIs feed the Windows Widgets Board (a Windows-hosted surface), with packaged-provider registration requirements for Win32. They do not let TaskbarEngine insert arbitrary custom interactive widgets into the taskbar. Do not replace this feature with a Widgets Board provider unless implementing a separate, optional product surface.

Open-source examples inspected in the research:

- Taskband startup/source: https://raw.githubusercontent.com/mnismi/taskband/main/src/main.rs
- Taskband taskbar/monitor discovery: https://raw.githubusercontent.com/mnismi/taskband/main/src/taskbar.rs
  - It detects `Shell_TrayWnd` and `Shell_SecondaryTrayWnd`, maps those HWNDs to monitors, creates app-owned bar windows and embeds them. This is useful implementation evidence, not a Microsoft support guarantee; taskbar child-window assumptions remain fragile across Explorer/Windows updates.
- BetterBar project: https://github.com/Renbo2024/better-bar
  - Its app-owned taskbar replacement uses the Win32 AppBar model, per-monitor DPI and a separate bar definition/panel placement model. This is a more supportable reference if an app-owned adjacent bar is acceptable, but it is not content inserted into the native taskbar.
- TaskbarWidgets project: https://github.com/pfcdev/TaskbarWidgets
  - Relevant project to inspect for a current custom taskbar widget runtime. Inspect actual source files and licenses before borrowing patterns or code.

Keep these three surfaces distinct in implementation and documentation:

1. Windows Widgets Board providers: system-hosted, packaged, documented, separate from this taskbar feature.
2. TaskbarEngine widgets: custom rendering owned by this app; needs an app-controlled host and a clear widget API.
3. Explorer injection/child-window/visual-tree modifications: compatibility techniques only, fragile and not a documented general-purpose embedding API.

## Architecture decision expected

Make a concrete choice based on the product's real requirement and current code. Document why.

Preferred reliability direction:

- No custom widget code should run inside Explorer.
- Keep rendering/lifecycle in a TaskbarEngine-owned host process and HWND(s), with clear ownership of Direct2D/DirectComposition resources, input, focus, accessibility and cleanup.
- Use the app's existing IPC where suitable; otherwise define a versioned protocol with explicit framing, validation, timeouts, ownership, restart and configuration snapshot semantics.
- Treat third-party native plugins as untrusted with respect to stability. A separate host process is the minimum isolation boundary; separate worker processes are needed if one plugin's hang/crash must not take down sibling widgets.
- Provide a supported app-owned adjacent/AppBar mode if stable Windows 10/11 behavior matters. If users insist on true native-taskbar placement, keep it as clearly labeled best-effort integration with OS/build support limits, and rebuild/rebind on Explorer restart.

A lower-disruption implementation may stabilize the present shared-overlay integration first, but only if it comes with verified lifecycle recovery and an explicit support statement. Do not silently call undocumented taskbar embedding “supported.” Do not build a large replacement architecture without a staged migration path for existing widgets/config.

## Required implementation properties

### Discovery and custom widget addition

- There must be a documented, repeatable custom-widget installation flow (directory/package location, filename/module rules, restart or live discovery behavior, architecture/runtime requirements, API version, and error reporting).
- Discover from an explicit configured/app-owned plugin directory, not an accidental working directory. Keep discovery deterministic, sorted, and observable in logs.
- Validate API major version, structure size, required callbacks, stable unique ID, and metadata before accepting a module. Reject incompatible modules safely and report why.
- Load/unload only after lifecycle callbacks, worker threads, COM objects, and graphics resources are stopped. Avoid unloading a faulted module if that would execute unsafe cleanup.
- A user must be able to discover and enable a newly installed widget from Settings without editing the GUI's hard-coded list. If dynamic settings schemas are not feasible, define a clear generic settings UI and provide a registry/catalog interface.
- Keep unknown per-widget settings during migration/save. Validate IDs and paths to avoid path traversal or unintended DLL loading.

### Rendering, input and accessibility

- Specify whether measurements and pointer coordinates are DIP or physical pixels; enforce one convention through all APIs.
- Define render-thread/UI-thread rules, callback duration expectations, invalidation/update scheduling, frame throttling, and device-loss behavior. Avoid redrawing every static widget every second when not needed.
- Define a widget bounds/hit-test contract that matches visible pixels on all supported DPI values and monitors.
- Define click, right-click, scroll, keyboard focus, tooltips, accessibility/UI Automation, and activation semantics. Visible pixels without usable input/accessibility are not complete.
- Test foreground overlap, taskbar auto-hide, secondary displays and taskbar placement within the declared support matrix.

### Lifecycle and recovery

- Explorer exit/restart/taskbar HWND destruction and recreation; host restart; display add/remove/rotation/resolution change; DPI change; graphics device loss; settings reload; enable/disable; process shutdown/logoff.
- Idempotent init/enable/disable/shutdown and cleanup. No duplicate subclass, timer, surface, or visual after repeated transitions.
- Atomic application of normalized configuration snapshots; no use-after-free when widget callbacks are in flight during config replacement.

### Settings and configuration

- One canonical layout representation, preferably a JSON array of stable widget IDs. Add an explicit schema version/migration from the current format without losing custom fields.
- Separate host enabled state from per-widget enabled state.
- GUI order and settings must be populated from discovered widget metadata/capabilities, not hard-coded to clock/sysmon/media.
- Config edit -> persistence -> engine notification -> widget reconfiguration must be traceable and diagnosable.
- Settings page must represent missing/invalid plugins, duplicate IDs, load errors and empty widget catalog in a useful way.

### Packaging and compatibility

- Update CMake, build, deploy, package and installer rules together. A clean packaged install must place host, runtime dependencies, built-ins, and custom-widget folder at the paths runtime actually uses.
- State supported Windows versions/builds, CPU architectures and required runtimes. Do not imply that a Windows App SDK widget API is available on a target simply because another Windows App SDK feature supports that OS.
- Gracefully disable unsupported integration paths and explain why in logs/UI.

## Implementation sequence

Proceed in small, reviewable phases. You may change entire target files if that is the clearest safe repair, but preserve existing unrelated user edits and explain file-wide rewrites in your summary.

1. **Baseline and diagnostics** — inspect diffs, plugin load order, config event shape, overlay exports/ownership, packaging paths and exact runtime symptom. Add precise logs for directory, each DLL accepted/rejected, API version, bridge resolution/version, HWND identities, DPI/monitor, config generation, lifecycle transitions and render/commit HRESULTs. Do not launch Explorer manipulation while diagnosing.
2. **Config and registry normalization** — select schema/version, migrate current config, fix settings model. Introduce a dynamic widget catalog based on validated metadata and declared settings. Keep old config working.
3. **Host/lifecycle reliability** — implement the architecture selected above: process/HWND boundaries, monitor/taskbar binding, Explorer restart detection and rebind, display/DPI/device loss recovery, thread-safe config replacement, idempotent cleanup.
4. **Built-in plugin contract and migration** — move clock/sysmon/media to the stable API without regressions; add a small sample/custom widget plugin that proves the public extension path. Add documentation for creating/installing it.
5. **GUI and package integration** — drive listing/order/settings from discovery; update CMake, scripts and installer; verify clean install layout and actionable error messages.
6. **Focused runtime acceptance** — execute only narrow checks needed to establish the acceptance items below. Do not run the full test suite unless asked. If the desktop session is unavailable, do not report runtime acceptance as passed; provide manual steps and say what remains unverified.

After each phase, report changed files, design decisions, evidence, and remaining risks. Stop only when the requested end-to-end behavior is achieved or an external blocker genuinely prevents it.

## Acceptance criteria

All must be addressed, with checked evidence or an explicit blocker:

1. **Built-ins visible:** clock, system monitor and media widget render on the declared supported surface; update behavior reflects live values/media events.
2. **Custom widget add path:** install a sample custom widget using documented steps; it appears in the catalog/settings without source changes to GUI or engine and can be enabled, disabled and removed cleanly.
3. **Interaction:** visible hit regions line up; click/scroll behavior works for sample widget; ordinary taskbar interactions outside widget regions still work; keyboard/accessibility behavior is defined and verified or a concrete documented limitation remains.
4. **Persistence:** custom and built-in order, enable state and settings survive app restart and schema migration; unknown settings are preserved.
5. **Recovery:** Explorer restart/taskbar recreation restores the widgets without restarting the whole app, or the selected app-owned architecture keeps its HWND independent and repositions correctly. DPI/display/device changes do not create stale duplicate surfaces.
6. **Isolation/cleanup:** a deliberately terminated sample host/worker follows the documented restart policy; Explorer remains alive; repeated enable/disable/shutdown leaves no stale visual, timer, subclass or loaded-module leak.
7. **Packaging:** clean build/deploy/package includes the right files and custom plugin directory; test with the packaged layout, not only the build tree.
8. **Evidence:** report exact commands/checks, OS/build/monitor/DPI used, log excerpts or screenshots for visible and interactive behavior, and clearly separate passed, failed and unverified items.

## Final response format

Return:

- Architecture chosen and why (supported app-owned surface vs best-effort native taskbar integration).
- User-visible behavior implemented, including exact custom-widget install/create workflow.
- Files/subsystems changed and any whole-file rewrites.
- Focused builds/runtime checks and direct evidence; no vague “works” claims.
- Known limitations/support matrix and work remaining.
- Confirmation that unrelated pre-existing working-tree edits were preserved.

Begin by inspecting the working tree and the research report, then trace the config and taskbar lifecycle before changing code.
