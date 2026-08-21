# 10 — TaskbarResize Plugin

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: `Modules/taskbar_resize/` (`taskbar_resize.dll`)

---

## 1. Purpose & Overview

The **`taskbar_resize`** plugin (`taskbar_resize.c` / `taskbar_resize.h`) allows users to customize the height, padding, margins, and icon spacing of the Windows 11 taskbar without replacing shell binaries.

```
Modules/taskbar_resize/
├── CMakeLists.txt
├── taskbar_resize.h
└── taskbar_resize.c
```

---

## 2. Configuration & Setting Descriptors

The plugin exports 4 setting descriptors to the WinUI 3 settings GUI:

| Setting Key | Label | Type | Default | Min | Max | Step | Description |
|---|---|---|---|---|---|---|---|
| `height` | Height | `int` | `48` | `24` | `72` | `1` | Desired taskbar height in logical pixels. |
| `padding` | Padding | `int` | `4` | `0` | `20` | `1` | Internal taskbar vertical padding. |
| `margins` | Margins | `int` | `0` | `0` | `20` | `1` | Horizontal desktop edge margins (floating look). |
| `icon_spacing` | Icon Spacing | `int` | `8` | `0` | `32` | `1` | Spacing between adjacent taskbar buttons. |

---

## 3. Implementation Mechanism

### A. Window Geometry Adjustment
- Calculates target pixel dimensions based on the display DPI:
  \[
  height_{scaled} = \frac{height_{logical} \times DPI}{96}
  \]
- Applies new dimensions using `SetWindowPos` on `Shell_TrayWnd` with `SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED`.

### B. Work Area Synchronization (`SPI_SETWORKAREA`)
To prevent maximized application windows from overlapping the resized taskbar or leaving dead gaps:
- Computes the new usable desktop rect:
  \[
  \text{work\_area.bottom} = \text{screen.bottom} - height_{scaled}
  \]
- Calls `SystemParametersInfoW(SPI_SETWORKAREA, 0, &work_area, SPIF_SENDCHANGE | SPIF_UPDATEINIFILE)` to notify all desktop top-level windows.

### C. Multi-Monitor Secondary Taskbars
- Enumerates secondary taskbars by locating all top-level windows of class `Shell_SecondaryTrayWnd`.
- Synchronizes height and work area margins across all attached active displays.

---

## 4. Reversion & Disable Contract

When `Disable()` or `Shutdown()` is invoked:
1. Queries cached native dimensions recorded during `Initialize()`.
2. Restores `Shell_TrayWnd` and all `Shell_SecondaryTrayWnd` windows to native dimensions via `SetWindowPos`.
3. Restores native desktop work area bounds via `SystemParametersInfoW(SPI_SETWORKAREA)`.
4. Leaves zero residual changes in Explorer.
