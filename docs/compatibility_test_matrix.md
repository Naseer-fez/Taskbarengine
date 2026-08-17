# TaskbarEngine Compatibility Test Matrix

This document defines the formal compatibility test matrix, requirements, and manual/automated verification procedures for TaskbarEngine across supported Windows versions, hardware configurations, display scaling factors, and system lifecycle events.

---

## 1. Operating System Compatibility Matrix

| OS Edition / Build | Build Range | Status | Behavior / Expected Result | Verification Method |
|---|---|---|---|---|
| **Windows 10** (21H2 / 22H2) | Builds <= 19045 | ❌ Unsupported | Detection flags `TE_COMPAT_UNSUPPORTED_BUILD`. Log warning emitted, plugins skipped, Explorer remains unaffected. | Automated Unit Test |
| **Windows 11 21H2** (Initial Release) | Build 22000 | ❌ Unsupported | Pre-XAML taskbar changes; flagged unsupported. Logged and blocked. | Automated / Manual |
| **Windows 11 22H2** (Moment 1-4) | Build 22621 | ✅ Supported Baseline | Full feature support (TaskbarResize, IconHover, IPC). Clean injection and subclassing. | Manual & CI |
| **Windows 11 23H2** | Build 22631 | ✅ Fully Supported | Standard deployment baseline. Full feature validation. | Manual & CI |
| **Windows 11 24H2** | Build 26100 | ✅ Fully Supported | Modern XAML taskbar architecture tested and certified. | Manual & CI |
| **Windows 11 Insider (Dev/Canary)** | Build 26200+ | ⚠️ Untested / Compatible | Engine detects build > `max_tested_build`. Emits `TE_COMPAT_UNTESTED_BUILD` warning, allows execution. | Automated & Manual |
| **Windows Server 2022 / 2025** | Any | ⚠️ Non-Desktop | Core loads if taskbar present; unsupported configuration logged. | Automated |

---

## 2. Display DPI & Multi-Monitor Matrix

| Configuration | DPI Scaling | Target Monitor(s) | Expected Behavior | Pass Criteria |
|---|---|---|---|---|
| **Standard 1080p** | 100% (96 DPI) | Single Display | 1:1 pixel rendering, icon bounds accurate, no blurring. | Icon layout matches UIA bounds within 0.5px |
| **High-DPI 1440p** | 125% (120 DPI) | Single Display | Dynamic DPI scaling applied, overlays scale smoothly. | No visual truncation or clipping |
| **4K UHD** | 150% (144 DPI) | Single Display | Crisp DirectComposition scaling, fonts & icons sharp. | Animation curves smooth @ 60+ FPS |
| **Ultra-High DPI 4K** | 200% (192 DPI) | Single Display | 2x integer scaling factors verified across plugins. | Proper border and icon margins |
| **Mixed-DPI Dual Monitor** | Primary: 150% (4K)<br>Secondary: 100% (1080p) | Multi-Monitor | Subclass tracks per-monitor DPI transitions (`WM_DPICHANGED`), state store handles monitor handles. | Moving mouse between monitors updates DPI seamlessly |
| **Triple Monitor (Surround/Span)** | Mixed 100% / 125% / 150% | Multi-Monitor | Secondary taskbars correctly subclassed or ignored without interfering with primary. | No handle leaks or subclass collisions |

---

## 3. Taskbar Layout & Configuration Variants

| Feature / Setting | Supported Variants | Test Procedure | Pass Criteria |
|---|---|---|---|
| **Taskbar Alignment** | Centered (Win11 Default)<br>Left-Aligned (Classic) | Toggle in Windows Settings: Personalization > Taskbar > Taskbar behaviors. | Icon coordinates correctly recalculated; hover magnification tracks true icon positions. |
| **Auto-Hide Taskbar** | Enabled<br>Disabled | Enable "Automatically hide the taskbar". Move cursor to bottom edge to trigger unhide. | Subclass proc receives mouse events only when taskbar is visible; no ghost hover triggers. |
| **Taskbar Badging & Overlays** | Apps with notification badges / progress bars | Open app with download progress (e.g. browser) or badge (Teams/Discord). | Magnification and overlay maintain badge positioning on icon canvas. |
| **System Tray Overflow** | Flyout open / closed | Click tray overflow chevron (`^`). | Taskbar bounds preserve tray region; geometry events do not crash tray menu. |

---

## 4. Shell & System Lifecycle Scenarios

| Event | Trigger Mechanism | Expected Recovery Sequence | Pass Criteria |
|---|---|---|---|
| **Explorer Crash / Restart** | `taskkill /f /im explorer.exe` followed by `explorer.exe` | Tray App detects Explorer death via heartbeat -> State transitions to `EXPLORER_DEAD` -> Waits for `Shell_TrayWnd` -> Re-installs CBT hook -> Re-initializes Engine. | Tray App remains alive, automatically re-hooks new Explorer within 2 seconds. |
| **Display Resolution / Topology Change** | Change display resolution in Windows Settings | `WM_DISPLAYCHANGE` dispatched to plugins -> Redraw triggered -> UIA icon bounds refreshed. | No stale monitor handles, zero visual corruption. |
| **System Sleep / Modern Standby** | Put PC to Sleep (`rundll32.exe powrprof.dll,SetSuspendState 0,1,0`) and Wake | Power broadcast hook pauses frame loop -> On resume, timers restart, DirectComposition device re-created if lost. | Zero CPU spikes upon resume, all hooks operational. |
| **Session Lock / Unlock (Win+L)** | Lock workstation, wait 5s, unlock | Subclass handles session change; animation loop self-cancels while locked. | Idle CPU remains 0.0% during session lock. |
| **Virtual Desktop Switch** | `Ctrl + Win + Left/Right` | Virtual desktop notification event received -> Active window taskbar items refreshed. | No crash or freeze during fast desktop switches. |

---

## 5. Verification Protocol & Quality Gates

1. **Pre-Build Verification**:
   - `te_version.h` and `te_version.c` compile with zero warnings under `/W4 /WX` and `-Wall -Wextra -Werror`.
   - Automated Catch2 test suite passes 100% of test cases:
     ```powershell
     .\build\mingw-debug\Tests\te_tests.exe -s
     ```

2. **Automated Compatibility Checks**:
   - `test_stability.cpp`: Verifies fault isolation, auto-disable after 3 faults, and 100 rapid enable/disable iterations.
   - `test_msg_filter.cpp`: Verifies Win32 message filtering registry, deduplication, and cleanup.
   - `test_timer.cpp`: Verifies UI-thread timer dispatch, cancellation, and bulk plugin teardown.
   - `test_config_roundtrip.cpp`: Verifies atomic hot-reload and invalid JSON resilience.

3. **Manual Validation Sign-Off**:
   - Run `bench_system.exe` on target test machine and verify real CPU/RAM metrics written to `benchmark_report.md`.
   - Verify Tray App cleanly starts, injects, and handles Explorer restarts without lingering zombie threads.
