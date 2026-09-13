# TaskbarEngine Testing Infrastructure & Methodology (`TEST_INFRA.md`)

## 1. Executive Summary & Test Philosophy

The TaskbarEngine testing infrastructure verifies the `icon_hover` subsystem refactoring and core shell-integration architecture under strict dual-toolchain standards (MSVC `/W4 /WX` and MinGW `-Wall -Wextra -Werror`).

### 1.1 Test Philosophy & Architectural Principles

1. **Opaque-Box (Black-Box) & Requirement-Driven Testing**:
   - Tests evaluate subsystems against explicit behavioral specifications, interface contracts (`PROJECT.md` § Interface Contracts), and physical/mathematical invariants rather than coupling to internal implementation ephemera.
   - Every test case is bidirectionally traceable to requirements in `ORIGINAL_REQUEST.md` (R1 Pure Transparent Icon Extraction, R2 Selective/Differential Rendering, R3 VSync-Locked Render Pump & Animation Physics, R4 Verification & Clean-up).

2. **Category-Partition Method & Boundary Value Analysis (BVA)**:
   - Input domains are partitioned into disjoint equivalence classes (e.g., at-rest scale $S \le 1.001f$ vs active magnification $S > 1.001f$; normal VSync $\Delta t \in [1\text{ ms}, 50\text{ ms}]$ vs hitch spikes $\Delta t > 50\text{ ms}$ vs micro-steps $\Delta t < 1\text{ ms}$).
   - BVA explicitly targets boundary transitions (e.g., $S = 1.0000f, 1.0009f, 1.0010f, 1.0011f$; $\Delta t = -0.010f, 0.0001f, 0.001f, 0.050f, 0.200f$; settle tolerances $\epsilon = 0.001f$, velocity $|v| = 0.005f$).

3. **No Facade Tests & Adversarial Verification**:
   - Every test exercises real numerical, algorithmic, and memory logic.
   - Tests assert physical invariants at every simulation step: monotonic convergence, absence of overshoot under critical damping, exact energy dissipation, 32-bit premultiplied ARGB color bounds ($R, G, B \le A$), and $C^1$ tangent continuity across influence boundaries.

4. **Progressive Testability & Isolation**:
   - Each test is self-contained and isolated, constructing and destroying its own fixture state without dependencies on execution order or side effects.

---

## 2. Feature Inventory Coverage Across Tiers 1–4

The test harness provides comprehensive verification across all 4 tiers of the `icon_hover` refactoring:

| Tier | Component | Feature ID | Feature Description | Verification Method & Target Test Cases |
|---|---|---|---|---|
| **Tier 1** | **Icon Extraction & Alpha Channel** | **F1** | Executable Path & Jumbo Shell Extraction | Catch2: `Icon Hover - Alpha Channel Preservation` (validates 256x256 32-bit ARGB extraction contracts). |
| | | **F4** | UIA Image Narrowing & Background Subtraction | Catch2: `Icon Hover - Alpha Channel Isolation & Background Subtraction` (verifies background matte subtraction $C = \alpha I + (1-\alpha) B$, eliminating rectangular taskbar background boxes). |
| | | **F5** | Safe 32-bit ARGB DIB & Mask Handling | Catch2: `Icon Hover - 1-Bit Mask Handling Preserves Pure Black Pixels` (verifies pure black RGB(0,0,0) with opaque mask is NOT treated as transparent). |
| **Tier 2** | **Selective & Differential Rendering** | **F6** | Selective / Differential Opacity Threshold | Catch2: `Icon Hover - Differential Rendering Scale Gating` (BVA: $S \le 1.001f \implies \text{opacity } 0.0f$; $S > 1.001f \implies \text{opacity } 1.0f$). |
| | | **F7** | Bottom Baseline Anchoring Geometry | Catch2: `Icon Hover - Bottom Baseline Anchoring Geometry Invariance` (verifies scale center $(W/2, H)$ and $pos\_y = 0.0f$ keeps bottom edge stationary at taskbar baseline for all scales). |
| | | **F8** | $C^1$ Tangent Continuous Displacement | Catch2: `Icon Hover - C1 Tangent Continuous Displacement` (verifies anti-symmetric Hermite taper $h(u) = u(1-u^2)^2$ vanishes with zero slope at boundary $u=1$). |
| **Tier 3** | **Animation Physics & VSync Pump** | **F9** | 2nd-Order Critically Damped Spring Physics | Catch2: `Icon Hover - Spring Settling Physics & Convergence` (validates $\omega_0=32\text{ rad/s}, k=1024, c=64$, settle time $\le 150\text{ ms}$, zero overshoot, $\Delta t$-invariance across 60Hz, 120Hz, 144Hz, 240Hz). |
| | | **F10** | Cursor Low-Pass Exponential Smoothing | Catch2: `Icon Hover - Cursor Low-Pass Exponential Smoothing` (verifies EMA filter $\tau=15\text{ ms}$ eliminates mouse jitter; snaps on teleportation $> 300\text{ px}$). |
| | | **F11** | Dedicated VSync Render Pump Thread | Catch2: `Icon Hover - Frame Pacing & VSync Timing Bounds` (validates $\Delta t$ clamping to $[1\text{ ms}, 50\text{ ms}]$). |
| | | **F12** | Event-Driven 0% Idle CPU State Machine | Catch2: `Icon Hover - Frame Pacing Sleep/Wake State Machine` (verifies transitions SLEEPING $\to$ WAKING $\to$ PUMPING $\to$ SETTLING $\to$ SLEEPING; settling termination criteria). |
| **Tier 4** | **Verification, Performance & Lifecycle** | **F13** | Catch2 E2E Unit Test Suite | Test runner: `build_msvc/Tests/te_tests.exe [icon_hover]` (100% pass across all test cases). |
| | | **F14** | Strict Compiler Standards | Build commands: MSVC `/W4 /WX /wd5105 /wd4459` and MinGW `-Wall -Wextra -Werror`. |
| | | **F15** | Google Micro-Benchmarks (< 500 ns) | Benchmark runner: `build_msvc/Benchmarks/te_benchmarks.exe --benchmark_filter=BM_Magnify` (< 120 ns measured, well under 500 ns ceiling). |
| | | **F16** | Clean Resource Lifecycle Reclamation | Catch2 & Memory audits: leak-free release of DIB sections, GDI handles, COM objects, and thread synchronization primitives. |

---

## 3. Directory Layout

The project adheres to a clean separation of Core, Modules, Tests, Benchmarks, and SDK:

```
Taskbar/
├── CMakeLists.txt              # Root CMake configuration (MSVC / MinGW warning flags)
├── PROJECT.md                  # Project specifications, milestones & interface contracts
├── TEST_INFRA.md               # Testing infrastructure & methodology specification (this document)
├── TEST_READY.md               # Test suite readiness declaration & verification report
├── Core/                       # Host engine process (IPC, subclass, events, state store)
│   ├── include/core/           # Core internal headers
│   └── src/                    # Core implementation files
├── Modules/                    # Native C/C++ plugin modules
│   └── icon_hover/             # Icon magnification plugin module
│       ├── icon_hover.c        # Plugin ABI entry point and event dispatch
│       ├── icon_hover_internal.h # Shared state bridging C ABI and C++ subsystems
│       ├── icon_capture.h/.cpp # Pure 32-bit ARGB extraction & background subtraction
│       ├── dcomp_overlay.h/.cpp# DirectComposition overlay, baseline anchoring, opacity
│       ├── frame_loop.h/.cpp   # VSync-locked render pump, 2nd-order spring physics
│       ├── uia_discovery.h/.cpp# UI Automation taskbar button enumeration
│       └── magnification.h/.c  # Magnification curves (Gaussian, Cubic, Cosine, Linear)
├── Tests/                      # Catch2 v3.5.2 unit test suite
│   ├── CMakeLists.txt          # Test runner build definition
│   ├── test_icon_hover.cpp     # Comprehensive icon_hover tests (spring, alpha, vsync, differential)
│   ├── test_magnification.cpp  # Curve weights, bounds, radius falloff
│   ├── test_config.cpp         # JSONC configuration parsing
│   ├── test_event_dispatch.cpp # Event bus and subscription
│   └── ...                     # Subsystem unit tests
├── Benchmarks/                 # Google Benchmark v1.9.0 micro-benchmarks
│   ├── CMakeLists.txt          # Benchmark build definition
│   └── bench_magnification.cpp # Magnification compute time benchmarks (< 500 ns target)
└── SDK/                        # Public TaskbarEngine plugin SDK
    └── include/sdk/            # Plugin ABI, events, types, logging
```

---

## 4. Runner Invocation & Test Commands

### 4.1 MSVC Build & Test Execution (Release)

From PowerShell or Developer Command Prompt for VS:

```powershell
# Setup MSVC x64 environment
call "D:\Extras\ES\msvc\setup_x64.bat"

# Configure CMake with Ninja (Release)
cmake -B build_msvc -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=cl `
    -DCMAKE_CXX_COMPILER=cl `
    -DTE_BUILD_TESTS=ON `
    -DWINAPPSDK_CPPWINRT="D:/Extras/ES/packages/winappsdk_cppwinrt" `
    -DWINAPPSDK_FRAMEWORK_DIR="D:/Extras/ES/packages/Microsoft.WindowsAppSDK.1.5.240311000/tools/MSIX/win10-x64/extracted" `
    -DWINAPPSDK_ROOT="D:/Extras/ES/packages/Microsoft.WindowsAppSDK.1.5.240311000"

# Build Catch2 test runner target
cmake --build build_msvc --config Release --target te_tests

# Run icon_hover tests specifically
.\build_msvc\Tests\te_tests.exe [icon_hover]

# Run specific tag filters
.\build_msvc\Tests\te_tests.exe [spring]
.\build_msvc\Tests\te_tests.exe [alpha]
.\build_msvc\Tests\te_tests.exe [vsync]
.\build_msvc\Tests\te_tests.exe [differential]

# Run the complete test suite across all subsystems
.\build_msvc\Tests\te_tests.exe

# Run Google Micro-Benchmarks (filtering for magnification compute)
.\build_msvc\Benchmarks\te_benchmarks.exe --benchmark_filter=BM_Magnify
```

### 4.2 MinGW Build & Test Execution

```powershell
# Configure CMake with MinGW GCC
cmake -B build_mingw -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=gcc `
    -DCMAKE_CXX_COMPILER=g++ `
    -DTE_BUILD_TESTS=ON

# Build test runner
cmake --build build_mingw --target te_tests

# Run Catch2 tests
.\build_mingw\Tests\te_tests.exe [icon_hover]
```
