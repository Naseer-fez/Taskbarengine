> Source of truth: Extracted from the implementation roadmap.

# Phase 5 — GUI + Benchmarks + CI + Release

---

## Purpose

Transform the engine into a **distributable product**. Build the WinUI 3
auto-generated settings GUI, set up the Azure DevOps CI pipeline, run full
system benchmarks, write the hand-written Markdown documentation series,
create the portable ZIP package, and validate all performance targets.

## High-Level Goal

A downloadable `TaskbarEngine-v1.0.0.zip` that a user can extract, run, and
use with a polished settings GUI — with all performance targets verified and
all documentation complete.

## Why This Phase Comes Now

All engine functionality is complete (Phase 1-4). This phase adds the
user-facing shell (GUI), productionization (CI, benchmarks), and
documentation. No engine logic is added.

## Components Implemented

| Component | Language | Description |
|---|---|---|
| WinUI 3 Settings GUI | C++/WinRT | Auto-generated settings pages from plugin `GetSettings()` metadata. Type-to-control mapping. Navigation by plugin. About page with version. |
| GUI ↔ Engine bridge | C++/C | GUI reads `config.jsonc` directly (for settings display). Writes modified values back. Queries plugin list + status via named pipe IPC. |
| System benchmark harness | C/C++ | Measures idle CPU (over 60s), RSS, startup time (cold), plugin load times, animation FPS, animation latency. Outputs Markdown report. |
| Azure DevOps Pipeline | YAML | Build matrix (MSVC Release + Clang-cl Debug+ASan), run Catch2 tests, run Google Benchmarks, build Doxygen docs, publish artifacts. |
| Doxygen configuration | Doxyfile | Generate HTML API reference from `///` comments in SDK + Core headers. |
| Hand-written Markdown docs | Markdown | The full 00-19 documentation series (01_System_Architecture through 19_Appendix). |
| Packaging script | PowerShell | `Scripts/package.ps1` — builds Release, copies binaries + config + docs into a ZIP. |
| Task Scheduler registration | C/PowerShell | First-run logic in Tray App: create scheduled task for auto-start. `Scripts/uninstall.ps1` to remove it. |
| Default config + README | Markdown/JSONC | User-facing `README.md`, `LICENSE`, default `config.jsonc` with comments explaining every setting. |

## Folder Structure Additions

```diff
 TaskbarEngine/
 ├── App/
 │   ├── src/
+│   │   ├── gui_main.cpp          # WinUI 3 window creation + hosting
+│   │   ├── settings_page.cpp     # Auto-generated settings from metadata
+│   │   ├── settings_page.h
+│   │   ├── about_page.cpp        # About page with version info
+│   │   └── scheduler.c           # Task Scheduler registration
 │   └── res/
+│       ├── app.pri               # WinUI 3 resources
+│       └── Styles/               # WinUI 3 styles/themes
 ├── Benchmarks/
+│   ├── bench_system.cpp          # System-level benchmark harness
+│   └── bench_config_parse.cpp    # Config parsing speed
 ├── Scripts/
+│   ├── package.ps1               # Build + package into ZIP
+│   └── uninstall.ps1             # Remove Task Scheduler entry
 ├── Docs/
+│   ├── 00_Project_Overview.md    # Updated from design_decisions.md
+│   ├── 01_System_Architecture.md
+│   ├── 02_Project_Structure.md
+│   ├── 03_Core_Manager.md
+│   ├── 04_Plugin_System.md
+│   ├── 05_Shared_SDK.md
+│   ├── 06_GUI.md
+│   ├── 07_Configuration.md
+│   ├── 08_Event_System.md
+│   ├── 09_Rendering.md
+│   ├── 10_Taskbar_Resize.md
+│   ├── 11_Icon_Hover.md
+│   ├── 12_Performance.md
+│   ├── 13_Benchmarking.md
+│   ├── 14_Testing.md
+│   ├── 15_API_Reference.md
+│   ├── 16_Build_System.md
+│   ├── 17_Deployment.md
+│   ├── 18_Future_Features.md
+│   └── 19_Appendix.md
+├── Doxyfile                       # Doxygen configuration
+├── azure-pipelines.yml           # Azure DevOps pipeline definition
+├── README.md                     # User-facing project README
+└── LICENSE                       # MIT license
```

## Public APIs Introduced

No new engine APIs. This phase consumes existing APIs from the GUI side.

## Internal Systems Created

| System | Description |
|---|---|
| Settings page generator | Reads `SettingDescriptor[]` from each plugin (via IPC → Engine → `GetSettings()`). Maps `SettingType` → WinUI 3 control. Generates a `StackPanel` of controls per plugin, wrapped in a `NavigationView`. |
| System benchmark runner | `bench_system.exe` — launches TaskbarEngine, measures CPU/memory over 60s idle, triggers mouse movement for animation FPS measurement, writes `benchmark_report.md`. |
| Packaging pipeline | PowerShell script: `cmake --build --config Release` → copy `*.exe`, `*.dll`, `config.jsonc`, `README.md`, `LICENSE` → `7z a TaskbarEngine-v1.0.0.zip` |

## Dependencies on Previous Phases

| Dependency | Source |
|---|---|
| All engine functionality | Phases 1-4 |
| Plugin `GetSettings()` metadata | Phase 2 (ABI), Phase 3+4 (implementations) |
| Named pipe IPC (for GUI → Engine queries) | Phase 3 |
| All plugin DLLs | Phases 3-4 |

## Unit Testing Strategy

- `test_settings_generation.cpp`: Given mock `SettingDescriptor` arrays, verify correct WinUI 3 control types are generated.
- `test_config_roundtrip.cpp`: GUI reads config, modifies a value, writes config, Engine hot-reloads — verify the value changed.
- **Manual GUI testing**: Visual verification of settings pages, toggle switches, sliders, comboboxes.
- **Full system integration test**: Fresh install → run → configure both plugins → restart Explorer → verify recovery → exit → verify clean state.

## Performance Considerations

- GUI launch time should be < 500 ms (WinUI 3 cold start is the bottleneck).
- GUI should not affect Engine performance (separate rendering, IPC only on user action).
- System benchmark must verify all targets from the architecture document (see table below).

### Performance Target Verification

| Metric | Target | Measurement Method |
|---|---|---|
| Idle CPU | 0% | `GetProcessTimes` over 60s, mouse stationary |
| Average CPU | < 0.5% | `GetProcessTimes` over 60s, typical interaction |
| Peak CPU | < 2% | `GetProcessTimes` during animation burst |
| Idle RAM | < 10 MB | `GetProcessMemoryInfo` (WorkingSetSize), all plugins loaded |
| Plugin load | < 5 ms | `QueryPerformanceCounter` around `LoadLibrary` + init |
| Startup | < 50 ms | `QueryPerformanceCounter` from hook install to all plugins enabled |
| Animation latency | < 2 ms | `QueryPerformanceCounter` from `WM_MOUSEMOVE` to `Commit()` |
| Taskbar redraw | < 1 ms | `QueryPerformanceCounter` around `SetWindowPos` |
| FPS | ≥ 60 | Frame time measurement during animation |

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| WinUI 3 hosting in a non-packaged app is complex | High — GUI may not load | Use Windows App SDK bootstrapper API for non-packaged deployment. Test on clean Windows 11 installs. |
| WinUI 3 cold start is slow (500ms+) | Low — user-facing only | Accept it. GUI is opened on demand, not at startup. |
| System benchmarks show a target is missed | Medium — need optimization | Phase 4 exit criteria should catch most issues. Phase 5 is the safety net. |
| Azure DevOps pipeline setup for Windows is complex | Low — one-time setup | Use `windows-latest` hosted agent. MSVC 2022 is pre-installed. |

## Deliverables

- [ ] WinUI 3 Settings GUI opens from tray icon, shows all plugin settings
- [ ] Settings changes in GUI are written to config.jsonc and hot-reloaded by Engine
- [ ] System benchmark report generated and all targets verified
- [ ] Azure DevOps Pipeline builds, tests, and publishes artifacts
- [ ] Doxygen API reference generated
- [ ] All 20 Markdown documentation files written (00-19 series)
- [ ] `TaskbarEngine-v1.0.0.zip` packaged with all binaries, config, docs
- [ ] Task Scheduler auto-start registration on first run
- [ ] `uninstall.ps1` script removes Task Scheduler entry
- [ ] README.md with installation, usage, and configuration instructions

## Exit Criteria (Definition of Done)

> **Phase 5 is complete when:**
> 1. A user can download `TaskbarEngine-v1.0.0.zip`, extract it, run `TaskbarEngine.exe`, open Settings from the tray icon, configure both plugins via the GUI, and see changes applied live.
> 2. All system benchmark targets are met (verified by `benchmark_report.md`).
> 3. Azure DevOps Pipeline passes: MSVC Release build + Clang-cl ASan build + all Catch2 tests + all Google Benchmarks.
> 4. Doxygen generates clean API reference with zero warnings.
> 5. All 20 Markdown documentation files are complete and cross-referenced.
> 6. The system passes a full integration test: install → configure → use → restart Explorer → recover → exit → verify clean state.
