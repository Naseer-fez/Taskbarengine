# 14 — Testing Strategy & Test Suite

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: `Tests/` (`te_tests.exe`)

---

## 1. Testing Philosophy & Framework

TaskbarEngine enforces an exhaustive automated testing strategy:
- **Framework**: Built on **Catch2 v3.7.1** integrated with CMake / CTest.
- **Deterministic**: Unit tests do not depend on system state, active user sessions, or network connections.
- **AddressSanitizer (ASan)**: All tests run cleanly under Clang-cl with `-fsanitize=address` to verify zero memory leaks, buffer overflows, or use-after-free conditions.
- **Fault Containment Validation**: Dedicated fault-injection mock plugins (`FaultPlugin`) simulate access violations and stack overflows to verify SEH isolation.

```
Tests/
├── CMakeLists.txt
├── test_config.cpp                 # Configuration parsing, getters, and fallbacks
├── test_config_roundtrip.cpp       # Live file overwrite & hot-reload roundtrip
├── test_crash_recovery.cpp         # Shell process termination & recovery machine
├── test_dpi.cpp                    # Per-monitor DPI scaling calculations
├── test_easing.cpp                 # Mathematical easing curve boundary verification
├── test_event_dispatch.cpp         # Synchronous event subscription and dispatch
├── test_icon_layout.cpp            # Taskbar button coordinate layout caching
├── test_icon_scales.cpp            # Icon magnification scale array calculations
├── test_ipc_client.cpp             # IPC client transaction helpers
├── test_ipc_protocol.cpp           # IPC header serialization and framing
├── test_jsonc.cpp                  # JSONC comment stripping and cJSON parsing
├── test_magnification.cpp          # Gaussian wave algorithms and distance functions
├── test_msg_filter.cpp             # Subclass window message filter table
├── test_plugin_loader.cpp          # Plugin discovery, vtable validation, lifecycle
├── test_ring_buffer.cpp            # Asynchronous log ring buffer wrap-around
├── test_settings_generation.cpp    # JSON schema to GUI control type translation
├── test_stability.cpp              # SEH exception containment with FaultPlugin
├── test_state_store.cpp            # Inter-plugin blackboard state publish/query
├── test_taskbar_resize.cpp         # Taskbar geometry calculations
├── test_timer.cpp                  # UI thread timer queue registration and ticks
├── test_tray.cpp                   # Tray notification icon lifecycle
└── test_tray_menu.cpp              # Context menu item dispatch
```

---

## 2. Test Execution

### A. Running Tests via CTest
```powershell
cd build_msvc
ctest -C Release --output-on-failure
```

### B. Running Tests Directly via Catch2 Runner
```powershell
# Run all tests
.\bin\te_tests.exe

# Run specific tag or test name
.\bin\te_tests.exe "[gui]"
.\bin\te_tests.exe "[config][roundtrip]"

# List all test cases
.\bin\te_tests.exe --list-tests
```

---

## 3. Key Test Categories

### A. Configuration & Hot-Reload (`test_config_roundtrip.cpp`)
- Verifies that modifying `%LOCALAPPDATA%\TaskbarEngine\config.jsonc` while the engine is running properly triggers diff detection and updates internal values without memory leaks.
- Verifies that saving invalid/corrupt JSON leaves existing valid settings active without crashing.

### B. Fault Containment & Stability (`test_stability.cpp`)
- Loads `fault_plugin.dll` which intentionally throws an access violation (`*(volatile int*)0 = 1`) inside `Enable()` and `OnEvent()`.
- Verifies that `TE_FaultFilter` captures the exception, quarantines the plugin, and continues test execution without terminating the test process.

### C. Settings Schema Generation (`test_settings_generation.cpp`)
- Verifies that all 6 setting types (`bool`, `int`, `float`, `string`, `enum`, `color`) serialize correctly into JSON and map accurately to WinUI 3 control expectations.
