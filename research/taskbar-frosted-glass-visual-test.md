# Taskbar Frosted Glass visual test

Date: 2026-11-17 (Asia/Calcutta)

## Result

**Failed acceptance test.** On this machine, SetWindowCompositionAttribute accepted ACCENT_ENABLE_ACRYLICBLURBEHIND on the primary Shell_TrayWnd, but the taskbar remained opaque and did not visibly sample or blur the bright pattern behind it. This is API acceptance only, not verified frosted glass.

## Environment and target

- OS: Windows 11 Home Single Language, 64-bit, version 10.0.26200, build 26200.
- Primary taskbar: HWND 0x80344, class Shell_TrayWnd, Explorer PID 22228, TID 31924.
- Test area: primary display, 1920 × 1080.
- Existing configured material values: Acrylic, opacity 0.65, tint 0x44000000, accent flags 2.
- Test policy passed the equivalent values: state 4 (ACCENT_ENABLE_ACRYLICBLURBEHIND), flags 2, gradient color 0x2C000000 (effective alpha from 0x44 × 0.65).

## Method and evidence

A temporary, borderless, animated checker pattern with magenta, lime, cyan, and yellow tiles was placed directly behind the taskbar. The root HWND policy was applied while the pattern was visible. SetWindowCompositionAttribute returned TRUE; no GetLastError value was interpreted after success. The taskbar remained opaque black/gray. Comparing the 1920 × 60 taskbar region in the before/after captures found zero changed pixel channels, while the checker pattern remained visible in the strip above it.

- Before: [taskbar-frosted-glass-visual-test-before.png](taskbar-frosted-glass-visual-test-before.png)
- After API accepted: [taskbar-frosted-glass-visual-test-after.png](taskbar-frosted-glass-visual-test-after.png)

The native policy restore call using ACCENT_DISABLED returned TRUE, and the temporary pattern window was closed. No Explorer restart or XAML element recreation was tested.

## Build

Command:

    cmake --build build_msvc --target taskbar_transparency --config Release

Result: successful.

## Implementation status

The transparency module still uses the Accent Policy path; it does not yet replace the taskbar XAML background brush. The repo has an Explorer hook payload and UI Automation support, but UI Automation only discovers taskbar controls and does not expose the XAML Shape.Fill brush. A real taskbar brush route needs an Explorer-side XAML Diagnostics/TAP component or another way to obtain the live XAML background element and update it on Explorer's XAML dispatcher. That adapter, brush restoration, and lifecycle handling remain unimplemented, so this is not a completed Frosted Glass implementation.

Runtime diagnostics were adjusted to label the current call as api_accepted and visual_pixels=unverified.
