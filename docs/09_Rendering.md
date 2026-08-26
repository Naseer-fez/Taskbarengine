# 09 — Rendering & DirectComposition Subsystem

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: `Modules/icon_hover/` (`dcomp_overlay.cpp`, `frame_loop.cpp`, `icon_capture.cpp`, `uia_discovery.cpp`)

---

## 1. DirectComposition Architecture

TaskbarEngine uses **Microsoft DirectComposition** (`dcomp.dll`) for 0-latency hardware accelerated visual enhancements over the Windows 11 taskbar:

- **Hardware Compositing**: Transforms and animations execute on the GPU compositor, entirely bypassing GDI and software bit-blits.
- **In-Process Composition**: Overlays attach directly to `Shell_TrayWnd` via `DCompositionCreateDevice` and `CreateTargetForHwnd`.
- **Zero-Copy Visual Transforms**: Scale and translation matrices are updated on GPU visual nodes; icon bitmaps are captured once into DirectComposition surfaces.

```mermaid
flowchart TD
    DCompDevice["IDCompositionDevice"] --> DCompTarget["IDCompositionTarget (Shell_TrayWnd)"]
    DCompTarget --> RootVisual["Root Visual (Full Taskbar Overlay)"]
    RootVisual --> IconVisual1["Icon Visual 1 (Scale + Translate Transform)"]
    RootVisual --> IconVisual2["Icon Visual 2 (Scale + Translate Transform)"]
    RootVisual --> IconVisualN["Icon Visual N (Scale + Translate Transform)"]

    IconVisual1 --> Surface1["DirectComposition Surface (Captured Bitmap)"]
    IconVisual2 --> Surface2["DirectComposition Surface (Captured Bitmap)"]
    IconVisualN --> SurfaceN["DirectComposition Surface (Captured Bitmap)"]

    FrameLoop["Animation Frame Loop (QPC)"] -->|Update Scale Matrices| RootVisual
    FrameLoop -->|DComposition Device Commit()| GPUCompositor["Desktop Window Manager (DWM / GPU)"]
```

---

## 2. Icon Discovery & Capture Pipeline

### A. Icon Location Discovery (`uia_discovery.cpp`)
- Discovers taskbar button coordinates via Windows UI Automation (UIA) and native window hierarchy inspection of the taskbar app list (`Taskbar.TaskListButtonAutomationPeer`).
- Caches icon bounding boxes (`RECT`) and monitors taskbar item insertion/removal events.

### B. High-Fidelity Bitmap Capture (`icon_capture.cpp`)
- Captures crisp, DPI-scaled icon bitmaps using Direct2D and WIC without triggering visual artifacts or taskbar flashing.
- Uploads captured icon surfaces into `IDCompositionSurface` textures associated with each `IDCompositionVisual`.

---

## 3. Animation Engine & Frame Loop (`frame_loop.cpp`)

- **High-Precision Timing**: Uses `QueryPerformanceCounter` (QPC) to compute sub-millisecond delta times (\(\Delta t\)).
- **Target Frame Rate**: Dynamically matches monitor refresh rate (60 Hz, 120 Hz, 144 Hz, 240 Hz).
- **Commit Boundary**: Work is batched and finalized via `IDCompositionDevice::Commit()`. Latency from mouse move to frame commit is < 2.0 ms.

### Zero-Idle-CPU Guarantee (Self-Canceling Loop)
The animation timer loop **only runs while the user is actively interacting**:
1. Mouse enters the taskbar $\rightarrow$ Animation frame loop starts.
2. Mouse moves $\rightarrow$ Easing targets update.
3. Mouse leaves the taskbar $\rightarrow$ Settle animation runs for remaining frames (~150 ms).
4. Icons reach rest state (\(scale = 1.0\)) $\rightarrow$ Animation loop cancels itself.
5. TaskbarEngine returns to **0.0% CPU utilization**.
