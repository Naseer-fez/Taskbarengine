# TaskbarEngine Phase 2 Overview

## Goals

Phase 2 gives TaskbarEngine a polished OS-backed acrylic taskbar and a small inline widget platform. The transparency plugin uses Windows `SetWindowCompositionAttribute` with `ACCENT_ENABLE_ACRYLICBLURBEHIND`. Widgets use the engine's existing DirectComposition overlay and Direct2D surfaces.

## Architecture

- `taskbar_transparency` owns taskbar accent policy and reapplies it after config, DPI, and display changes.
- `icon_hover` remains the shared DirectComposition resource owner. It exposes a narrow widget bridge for the common visual root and shared D2D/DComp devices. A separate widget visual layer stays visible while icon visuals fade.
- `widget_host` discovers widget DLLs under `Widgets/`, manages their lifecycle, per-widget surfaces, layout, input delegation, reorder animation, and persistence.
- The widget ABI is a C17 function table. Each widget is a separate DLL. Internal rendering can use C++ COM APIs.
- The Settings app receives widget-host descriptors through the existing settings IPC and writes the top-level `widgets` config section. Config reload events for that section are routed to `widget_host`.

## Built-in Widgets

The initial set is `clock`, `sysmon`, and `media`. It uses local Win32 APIs and the existing GSMTC source. No widget makes network requests. Weather is deferred. System monitoring uses `GetSystemTimes` and `GlobalMemoryStatusEx`; WMI is not used.

## Limits

Widgets run in Explorer. The host uses SEH around calls into widget DLLs and disables callbacks that raise a structured exception. This cannot isolate hangs, arbitrary heap corruption, or every process-level fault. The host and its visual layer are designed to stay idle between updates and to remain within the phase budget of less than 0.5% CPU and 15 MB working set; those budgets require measurement on supported Windows systems.

The Windows accent API does not expose supported independent controls for acrylic noise intensity or luminosity blend. Windows determines those visual properties.
