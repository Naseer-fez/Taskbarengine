# Project: TaskbarEngine icon_hover Module Refactor

## Architecture
TaskbarEngine's `icon_hover` module provides smooth, macOS Dock-style magnification physics and DirectComposition overlay rendering for Windows 10 and 11 taskbar icons. The subsystem is structured into modular components:

1. **Pure Transparent Icon Extraction (`icon_capture.h`, `icon_capture.cpp`)**:
   - 4-tier transparent extraction pipeline:
     - Tier 1: Real executable path via PID (`QueryFullProcessImageNameW`) or path detection -> `SHGetFileInfoW` + `SHGetImageList(SHIL_JUMBO)` (256x256 ARGB).
     - Tier 2: Packaged app AUMID via `SHCreateItemInKnownFolder(FOLDERID_AppsFolder)` + `IShellItemImageFactory::GetImage`.
     - Tier 3: Top-level HWND query via `WM_GETICON` (BIG, SMALL2, SMALL) and `GetClassLongPtrW(hwnd, GCLP_HICON)`.
     - Tier 4: UIA child `UIA_ImageControlTypeId` narrowing + background subtraction ($C = \alpha I + (1-\alpha) B$).
   - 1-bit `hbmMask` preservation in `ConvertHIconToBitmap` to ensure pure black `RGB(0,0,0)` artwork pixels are not treated as transparent.
   - Thread-safe caching with `SRWLOCK`.

2. **Selective / Differential Rendering (`dcomp_overlay.h`, `dcomp_overlay.cpp`)**:
   - DirectComposition visual tree managing per-icon visual nodes.
   - Opacity gating: icons at rest ($S \le 1.001f$) have opacity 0.0f (or detached from root visual); magnifying/settling icons ($S > 1.001f$) have opacity 1.0f.
   - Bottom baseline anchoring: scale center at $(W/2, H)$ and $pos\_y = 0.0f$, keeping the bottom edge stationary at the taskbar baseline for all scales.
   - $C^1$ continuous tangent displacement: Hermite anti-symmetric displacement $h(u) = u(1-u^2)^2$ vanishing smoothly at the influence radius boundary.

3. **VSync-Locked Animation Pump & Physics (`frame_loop.h`, `frame_loop.cpp`)**:
   - Dedicated high-priority animation worker thread (`THREAD_PRIORITY_HIGHEST` + MMCSS `DisplayPostProcessing`).
   - Paced via `DwmFlush()` / `IDXGIOutput::WaitForVBlank`.
   - 2nd-order critically damped spring system: $\ddot{x} = -k(x - x_{target}) - c \cdot v$ ($\omega_0 = 32\text{ rad/s}$, $k = 1024, c = 64$) with monotonic convergence and zero overshoot.
   - Low-pass exponential smoothing on cursor coordinates ($\tau = 15\text{ ms}$, teleport snap $> 300\text{ px}$).
   - Event-driven state machine with 0.000% CPU consumption when idle or settled.

4. **Integration, Build Standards & Lifecycle Verification (`icon_hover.c`, `Tests/`, `Benchmarks/`)**:
   - Strict warning standards: MSVC (`/W4 /WX /wd5105 /wd4459`), MinGW (`-Wall -Wextra -Werror`).
   - Catch2 unit test suite (`te_tests.exe [icon_hover]`) with 853 assertions covering Tiers 1-4.
   - Google Benchmark (`te_benchmarks.exe --benchmark_filter=BM_Magnify`) confirming compute time < 500 ns.
   - Complete reclamation of all GDI, DirectComposition, COM, and thread primitives upon exit.

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | F1 | Executable Path & Jumbo Shell Extraction (256x256 32-bit ARGB) | M1 | TEST_INFRA §2 |
| 2 | F2 | Packaged App AUMID via ShellItem Image Factory | M1 | TEST_INFRA §2 |
| 3 | F3 | Top-Level HWND Query via WM_GETICON & GCLP_HICON | M1 | TEST_INFRA §2 |
| 4 | F4 | UIA Image Narrowing & Background Subtraction Fallback | M1 | TEST_INFRA §2 |
| 5 | F5 | Safe 32-bit ARGB DIB & Mask Handling (Pure Black Preservation) | M1 | TEST_INFRA §2 |
| 6 | F6 | Selective / Differential Opacity Threshold (Scale Gating S > 1.001f) | M2 | TEST_INFRA §2 |
| 7 | F7 | Bottom Baseline Anchoring Geometry (scale center (W/2, H), pos_y = 0.0f) | M2 | TEST_INFRA §2 |
| 8 | F8 | C^1 Tangent Continuous Displacement (Hermite Anti-Symmetric Taper) | M2 | TEST_INFRA §2 |
| 9 | F9 | 2nd-Order Critically Damped Spring Physics (omega_0 = 32 rad/s, k=1024, c=64) | M3 | TEST_INFRA §2 |
| 10 | F10 | Cursor Low-Pass Exponential Smoothing (tau = 15 ms, teleport snap > 300 px) | M3 | TEST_INFRA §2 |
| 11 | F11 | Dedicated VSync Render Pump Thread (DwmFlush / WaitForVBlank) | M3 | TEST_INFRA §2 |
| 12 | F12 | Event-Driven 0% Idle CPU State Machine (Sleep when settled) | M3 | TEST_INFRA §2 |
| 13 | F13 | Catch2 E2E Unit Test Suite (853 assertions, Tiers 1-4) | M4 | TEST_INFRA §2 |
| 14 | F14 | Strict Compiler Standards (MSVC /W4 /WX, MinGW -Wall -Wextra -Werror) | M4 | TEST_INFRA §2 |
| 15 | F15 | Google Micro-Benchmarks (Magnify compute time < 500 ns) | M4 | TEST_INFRA §2 |
| 16 | F16 | Clean Resource Lifecycle Reclamation (DIB, GDI, COM, DComp surfaces) | M4 | TEST_INFRA §2 |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Pure Transparent Icon Extraction | Features F1, F2, F3, F4, F5 in `icon_capture.cpp` & `icon_capture.h`. 4-tier transparent extraction, hbmMask handling, background subtraction, thread-safe caching. | None | DONE (Gate 2 Passed: 455/455 tests pass, 0 GDI leaks, CLEAN audit) |
| M2 | Selective / Differential Rendering | Features F6, F7, F8 in `dcomp_overlay.cpp` & `dcomp_overlay.h`. Opacity scale gating (S > 1.001f), baseline anchoring at (W/2, H), C^1 continuous tangent displacement. | M1 | DONE (Gate 3 Passed: 90/90 [differential], 503/503 [icon_hover], 2,180/2,180 engine pass, CLEAN audit) |
| M3 | VSync-Locked Animation Pump & Physics | Features F9, F10, F11, F12 in `frame_loop.cpp` & `frame_loop.h`. Critically damped spring, cursor smoothing, VSync timing, 0% idle CPU. | M2 | DONE (Gate 4 Passed: 744/744 [icon_hover], 2,421/2,421 engine pass, BM_Magnify < 80 ns, CLEAN audit) |
| M4 | Integration, Performance & Clean-up | Features F13, F14, F15, F16 across `icon_hover` module, tests, and benchmarks. Dual-toolchain builds, Catch2 pass, Google Benchmark < 500 ns, resource leak-free reclamation. | M1, M2, M3 | DONE (Gate 5 Passed: 1,309/1,309 [icon_hover], 2,986/2,986 engine pass, BM_Magnify 35-108 ns, CLEAN audit) |

## Interface Contracts
### `icon_capture.h` <-> `dcomp_overlay.cpp`
```cpp
struct TE_TaskbarItemInfo {
    HWND hwnd;
    DWORD process_id;
    const wchar_t* app_id;
    int icon_index;
    RECT screen_bounds;
    IUIAutomationElement* uia_element;
};

HBITMAP TE_IconCaptureExtract(const TE_TaskbarItemInfo* item, int target_w, int target_h);
void TE_IconCaptureClearCache(void);
```

### `frame_loop.h` <-> `dcomp_overlay.h`
```cpp
struct TE_VisualState {
    float scale;
    float offset_x;
    float offset_y;
    float opacity;
};

void TE_DCompOverlayUpdateVisuals(const TE_VisualState* states, size_t count);
```

### `frame_loop.h` Lifecycle
```cpp
bool TE_FrameLoopStart(void);
void TE_FrameLoopStop(void);
void TE_FrameLoopNotifyCursor(int cursor_x, int cursor_y);
void TE_FrameLoopNotifyHoverExit(void);
```

## Code Layout
```
Modules/icon_hover/
├── icon_hover.c              # Plugin ABI entry point and event dispatch
├── icon_hover_internal.h      # Shared state bridging C ABI and C++ subsystems
├── icon_capture.h            # Icon capture declarations & interfaces
├── icon_capture.cpp          # 4-tier transparent icon extraction & alpha subtraction
├── dcomp_overlay.h           # DirectComposition visual overlay declarations
├── dcomp_overlay.cpp         # DirectComposition visual tree, differential opacity & anchoring
├── frame_loop.h              # Render pump & animation declarations
├── frame_loop.cpp            # VSync render pump, 2nd-order spring physics, cursor filter
├── uia_discovery.h           # Taskbar UI Automation discovery declarations
├── uia_discovery.cpp         # UI Automation element tracking
├── magnification.h           # Mathematical magnification curves
└── magnification.c           # Magnification curve implementations
```
