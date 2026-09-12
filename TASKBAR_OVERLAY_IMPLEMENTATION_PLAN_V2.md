# TASKBAR_OVERLAY_IMPLEMENTATION_PLAN_V2.md
# Technical Consistency Review & Implementation Specification

> **Status:** Implementation-Ready Design Review  
> **Source Baseline:** TASKBAR_OVERLAY_IMPLEMENTATION_PLAN (V1)  
> **Target Subsystem:** TaskbarEngine `icon_hover` Module & DirectComposition Pipeline  

---

## Executive Summary & Design Review Result

This document performs an exhaustive technical consistency review of the TaskbarEngine icon overlay architecture. All core approved architectural tenets from V1 remain preserved:
1. `Shell_TrayWnd` remains the single authoritative owner of taskbar geometry and native icons.
2. The overlay window remains a transparent `WS_POPUP` DirectComposition rendering surface (not a Win32 child).
3. Real taskbar icons remain visible in place at all times underneath the overlay.
4. Overlay visual opacity remains gated at rest (`scale <= 1.001` -> opacity 0.0).
5. All animation coordinates, visual bounds, and scaling transforms are derived from authoritative Win32 and UIA measurements rather than heuristics or hardcoded offsets.

### Design Review Verdict
- **Approved Unchanged:**
  - Opacity scale-gating concept (`TE_DIFF_SCALE_THRESHOLD = 1.001f`).
  - 2nd-order critically damped spring physics ($\omega_0 = 32.0\text{ rad/s}$) and cursor smoothing filter ($\tau = 15\text{ ms}$).
  - VSync-locked render pump thread with `DwmFlush()`.
  - 4-tier transparent icon capture engine producing 32-bit premultiplied ARGB bitmaps.
  - Bridge discovery methodology mirroring `taskbar_resize.c` (`DesktopWindowContentBridge` enumeration).
- **Clarified:**
  - Scale anchor mathematics with explicit 4-tier coordinate space derivation.
  - Bitmap capture data flow: surface size remains aligned with button bounds to maintain subpixel bilinear filter quality without premature downsampling or aspect distortion.
  - 5-layer fail-closed UIA classifier separating shell controls from true application buttons.
  - DPI-invariant, ratio-based fallback for glyph rect discovery.
  - Lifecycle state machine and generational cache invalidation during taskbar resizing (48px -> 32px).
  - Explicit numerical acceptance tests spanning heights, DPIs, and edge cases.
- **Requires Implementation Change:**
  - Addition of `TE_TaskbarGeometryInfo` struct and monotonic generation counter.
  - Separation of `buttonRect` and `glyphRect` inside `TE_IconElementInfo`.
  - Named headroom constant `TE_HOVER_HEADROOM_BASE_PX` replacing inline magic numbers.
  - Recalibration of DirectComposition transform origin (`SetCenterY`) from local button bottom to taskbar baseline.

---

## 1. Scale Anchor Mathematics & Coordinate-Space Derivation

### 1.1 Formal Coordinate Space Definitions

To eliminate any ambiguity regarding matrix transformations, four distinct coordinate systems are formally defined:

| Coordinate System | Origin $(0, 0)$ | Domain | Usage |
|---|---|---|---|
| **SCREEN** | Top-left of virtual desktop display area. | Physical Pixels | Win32 APIs (`GetWindowRect`), UI Automation (`get_CurrentBoundingRectangle`), Cursor input (`ClientToScreen`). |
| **OVERLAY** | Top-left of the overlay popup window client rect. | Physical Pixels | Overlay window surface, DirectComposition root visual target. Note: $Y_{\text{OVERLAY}} = 0$ corresponds to $Y_{\text{SCREEN}} = \text{taskbarRect.top} - \text{headroom\_y}$. |
| **VISUAL-LOCAL** | Top-left of the individual icon's `IDCompositionVisual`. | Physical Pixels | Visual content boundary, surface bitmap mapping, transform origin (`SetCenterX`, `SetCenterY`). |
| **SURFACE-LOCAL** | Top-left of the `IDCompositionSurface` allocation. | Texels | Direct2D render target drawing (`DrawBitmap`). |

### 1.2 Mathematical Derivation of Scale Anchor

In DirectComposition, calling `IDCompositionScaleTransform::SetCenterX(cx)` and `SetCenterY(cy)` establishes the fixed center of dilation $(c_x, c_y)$ **in VISUAL-LOCAL coordinates**.

For an affine scale transform matrix $M(S_x, S_y, c_x, c_y)$, any visual-local point $(x, y)$ transforms to $(x', y')$ according to:
$$x' = c_x + S_x \cdot (x - c_x)$$
$$y' = c_y + S_y \cdot (y - c_y)$$

When composed with an `IDCompositionTranslateTransform` with offsets $(\Delta x, \Delta y)$, the final position inside the parent visual (the OVERLAY space) is:
$$X_{\text{OVERLAY}} = X_{\text{visual\_base}} + c_x + S_x \cdot (x - c_x) + \Delta x$$
$$Y_{\text{OVERLAY}} = Y_{\text{visual\_base}} + c_y + S_y \cdot (y - c_y) + \Delta y$$

#### Step 1: Known Authoritative Quantities in SCREEN Space
- `taskbarRect`: Bounding rectangle of `Shell_TrayWnd` $[L_{\text{tb}}, T_{\text{tb}}, R_{\text{tb}}, B_{\text{tb}}]$
- `buttonRect`: Bounding rectangle of the taskbar button $[L_{\text{btn}}, T_{\text{btn}}, R_{\text{btn}}, B_{\text{btn}}]$
- `glyphRect`: Discovered or computed icon glyph rectangle $[L_{\text{gl}}, T_{\text{gl}}, R_{\text{gl}}, B_{\text{gl}}]$
- `headroom_y`: Physical upward headroom pixels above taskbar top edge
- `overlayRect`: Defined as $[L_{\text{tb}}, T_{\text{tb}} - \text{headroom\_y}, R_{\text{tb}}, B_{\text{tb}}]$

#### Step 2: Mapping to OVERLAY Coordinates
The overlay window top-left in screen coordinates is:
$$O_x = L_{\text{tb}}$$
$$O_y = T_{\text{tb}} - \text{headroom\_y}$$

The glyph center in OVERLAY coordinates is:
$$\text{glyphCenterX}_{\text{OVERLAY}} = \frac{L_{\text{gl}} + R_{\text{gl}}}{2} - O_x$$
$$\text{glyphCenterY}_{\text{OVERLAY}} = \frac{T_{\text{gl}} + B_{\text{gl}}}{2} - O_y$$

The taskbar baseline in OVERLAY coordinates is:
$$\text{baseline}_{\text{OVERLAY}} = B_{\text{tb}} - O_y = B_{\text{tb}} - (T_{\text{tb}} - \text{headroom\_y}) = (B_{\text{tb}} - T_{\text{tb}}) + \text{headroom\_y}$$

#### Step 3: Determining Visual Placement & Local Transform Origin
Let the visual surface size match the button bounding dimensions:
$$W_{\text{surf}} = R_{\text{btn}} - L_{\text{btn}}$$
$$H_{\text{surf}} = B_{\text{btn}} - T_{\text{btn}}$$

To align the visual's content center precisely with the physical icon glyph center on screen:
$$X_{\text{visual\_base}} = \text{glyphCenterX}_{\text{OVERLAY}} - \frac{W_{\text{surf}}}{2}$$
$$Y_{\text{visual\_base}} = \text{glyphCenterY}_{\text{OVERLAY}} - \frac{H_{\text{surf}}}{2}$$

These base offsets are assigned to the visual once during visual tree construction:
```cpp
s_icon_visuals[i]->SetOffsetX(X_visual_base);
s_icon_visuals[i]->SetOffsetY(Y_visual_base);
```

#### Step 4: Deriving Anchor in VISUAL-LOCAL Coordinates
We require that scaling causes zero displacement at the taskbar bottom baseline ($\text{baseline}_{\text{OVERLAY}}$).
In VISUAL-LOCAL coordinates, the baseline is located at:
$$c_y = \text{baseline}_{\text{OVERLAY}} - Y_{\text{visual\_base}}$$
Substituting the definitions of $\text{baseline}_{\text{OVERLAY}}$ and $Y_{\text{visual\_base}}$:
$$c_y = (B_{\text{tb}} - O_y) - \left( \frac{T_{\text{gl}} + B_{\text{gl}}}{2} - O_y - \frac{H_{\text{surf}}}{2} \right)$$
$$c_y = B_{\text{tb}} - \frac{T_{\text{gl}} + B_{\text{gl}}}{2} + \frac{H_{\text{surf}}}{2}$$

The horizontal scale anchor is centered across the visual:
$$c_x = \frac{W_{\text{surf}}}{2}$$

#### Proof of Baseline Invariance:
Evaluate $Y_{\text{OVERLAY}}$ for a point on the baseline ($y = c_y$) under arbitrary scale $S$:
$$Y_{\text{OVERLAY}}(c_y) = Y_{\text{visual\_base}} + c_y + S \cdot (c_y - c_y) + \Delta y = Y_{\text{visual\_base}} + c_y + \Delta y$$
Since $\Delta y = 0.0\text{f}$ (vertical translation is zero in baseline physics):
$$Y_{\text{OVERLAY}}(c_y) = Y_{\text{visual\_base}} + (\text{baseline}_{\text{OVERLAY}} - Y_{\text{visual\_base}}) = \text{baseline}_{\text{OVERLAY}}$$
The baseline point remains strictly invariant under all scale values $S \in [1.0, 2.0]$.

Evaluate the glyph top edge under scale $S$:
At rest ($S = 1.0$), top is $y_{\text{top}} = \frac{H_{\text{surf}}}{2} - \frac{B_{\text{gl}} - T_{\text{gl}}}{2}$.
Under scale $S > 1.0$:
$$y'_{\text{top}} = c_y + S \cdot (y_{\text{top}} - c_y) = c_y - S \cdot (c_y - y_{\text{top}}) < y_{\text{top}}$$
Because $c_y > y_{\text{top}}$, $(y_{\text{top}} - c_y)$ is negative. Multiplying by $S > 1.0$ displaces the top edge in the negative local direction (upward towards screen top, into the headroom area), matching the natural growth of macOS Dock magnification.

---

## 2. Bitmap Cropping vs Surface Sizing & Pipeline Analysis

### 2.1 Codebase Audit of Existing Extraction Pipeline
Inspection of `Modules/icon_hover/icon_capture.cpp` confirms:
- `TE_IconCaptureExtract()` produces a normalized **256x256 32-bit ARGB premultiplied bitmap**:
  - **Tier 1 (`ExtractTier1FromPath`)**: Retrieves `SHIL_JUMBO` image list and converts via `ConvertHIconToBitmap(hicon, 256, 256)`.
  - **Tier 2 (`ExtractTier2Packaged`)**: Uses `IShellItemImageFactory::GetImage({256, 256})` and normalizes into 256x256 DIB.
  - **Tier 3 (`ExtractTier3FromHwnd`)**: Calls `WM_GETICON`/`GCLP_HICON` and paints into 256x256 DIB via `DrawIconEx`.
  - **Tier 4 (`ExtractTier4ScreenCapture`)**: Crops screen to `crop_rect` matching UIA image child, performs alpha subtraction, and uses `SoftwareScale32()` to resample into 256x256.

### 2.2 Data Flow from Extraction to Visual Presentation

```
[Win32 Shell / App / Screen DC]
               │
               ▼  TE_IconCaptureExtract()
[HBITMAP: 256x256 Premultiplied ARGB DIB]
               │
               ▼  ID2D1RenderTarget::CreateBitmap()
[D2D Bitmap: 256x256 BGRA Texels]
               │
               ▼  rt->DrawBitmap(dest_rect = [0, 0, W_surf, H_surf])
[IDCompositionSurface: W_surf x H_surf Texels]
               │
               ▼  IDCompositionVisual::SetContent()
[DirectComposition Visual Tree Node]
               │
               ▼  GPU Rasterizer + Bilinear Interpolation
[Display Screen Framebuffer]
```

### 2.3 Surface Sizing Resolution
In V1, it was proposed to allocate `IDCompositionSurface` at 24x24 pixels (the glyph rect size).
**Technical Flaw Identified:** If an `IDCompositionSurface` is allocated at 24x24 texels:
1. Direct2D immediately downsamples the 256x256 master icon to 24x24 upon load.
2. During hover magnification (e.g. $S = 1.35$), DirectComposition must upscale that 24x24 texture to $\approx 32.4$ screen pixels using linear filtering, causing severe text/edge blurring and aliasing.
3. The extracted master bitmap from Tiers 1-3 is the *application's isolated icon asset* with natural transparent borders, not a capture of the taskbar button background.

**V2 Approved Solution:**
- Keep surface dimensions sized to the button slot ($W_{\text{surf}} \times H_{\text{surf}}$, typically 40x40 to 48x48 depending on DPI).
- Render the 256x256 asset into the surface using Direct2D bilinear downsampling.
- Center the visual in OVERLAY space on `glyphCenter`.
- This preserves maximum asset fidelity, provides natural subpixel anti-aliasing during magnification, and requires **zero changes** to `icon_capture.cpp` or `icon_capture.h`.

---

## 3. Glyph Discovery Fallback & Geometry Validation

### 3.1 UIA Image Child Discovery Lifecycle
Under Windows 11 XAML taskbars, app buttons host an internal `Image` control (`UIA_ImageControlTypeId`).
- **Success Condition:** `btn->FindFirst(TreeScope_Children, image_cond, &img)` returns an element whose `CurrentBoundingRectangle` is non-empty, has positive width/height, and is contained within `buttonRect`.
- **Failure Conditions:**
  - Non-XAML legacy buttons (Windows 10 compatibility fallback).
  - Web/PWA shortcuts where the icon is rendered as a composite Path/Canvas rather than an Image control.
  - Explorer UI thread busy during rapid window launches.
  - Custom shell modifications or classic theme injectors.

### 3.2 Strict Validation Rules for `glyphRect`
A candidate `glyphRect` obtained from UIA must satisfy all of the following:
1. **Positive Area:** $(R_{\text{gl}} - L_{\text{gl}}) \ge 8$ and $(B_{\text{gl}} - T_{\text{gl}}) \ge 8$.
2. **Containment / Overlap:** $L_{\text{gl}} \ge L_{\text{btn}} - 2$, $T_{\text{gl}} \ge T_{\text{btn}} - 2$, $R_{\text{gl}} \le R_{\text{btn}} + 2$, $B_{\text{gl}} \le B_{\text{btn}} + 2$.
3. **Plausible Aspect Ratio:** $0.6 \le \frac{W_{\text{gl}}}{H_{\text{gl}}} \le 1.4$.
4. **Plausible Size Ratio:** $0.35 \le \frac{W_{\text{gl}}}{W_{\text{btn}}} \le 0.90$.

If any check fails, the candidate rect is discarded and the fallback is engaged.

### 3.3 DPI-Invariant Fallback Specification
Rather than assuming a hardcoded 8px border, the fallback is derived strictly from measured button geometry:

$$\text{targetGlyphDim} = \text{round}(H_{\text{btn}} \times 0.60)$$
$$\text{insetX} = \frac{W_{\text{btn}} - \text{targetGlyphDim}}{2}$$
$$\text{insetY} = \frac{H_{\text{btn}} - \text{targetGlyphDim}}{2}$$
$$L_{\text{gl}} = L_{\text{btn}} + \text{insetX}, \quad R_{\text{gl}} = L_{\text{gl}} + \text{targetGlyphDim}$$
$$T_{\text{gl}} = T_{\text{btn}} + \text{insetY}, \quad B_{\text{gl}} = T_{\text{gl}} + \text{targetGlyphDim}$$

Because $W_{\text{btn}}$ and $H_{\text{btn}}$ are already physical pixel dimensions scaled by Windows for the active monitor DPI, the 60% ratio maintains exact proportional centering across 100%, 125%, 150%, 175%, and 200% scaling.

---

## 4. Layered Shell Control Classifier

To ensure Start, Search, Task View, Widgets, and System Tray elements never contaminate the app magnification pipeline, a 5-layer classifier is implemented in `uia_discovery.cpp`.

```
[Candidate UIA Element]
           │
           ▼
[Layer 1: ControlType Check] ──(Not Button)──────────────► [REJECT: UNKNOWN]
           │ (Is Button)
           ▼
[Layer 2: Identifier Substring Match] ──(Matches Shell)──► [REJECT: SHELL_CONTROL]
           │ (Clean IDs)
           ▼
[Layer 3: Ancestor Lineage Check] ──(In Tray Tree)───────► [REJECT: SYSTEM_TRAY]
           │ (Taskbar Content Area)
           ▼
[Layer 4: Icon Child Verification] ──(No Icon & No AppID)► [REJECT: UNKNOWN]
           │ (Has Icon / Valid App)
           ▼
[Layer 5: Dimensional Sanity Check] ──(Abnormal Aspect)──► [REJECT: UNKNOWN]
           │ (Plausible Geometry)
           ▼
     [ACCEPT: APP_ICON]
```

### Layer Details:
1. **Layer 1 (ControlType):** Must be `UIA_ButtonControlTypeId`.
2. **Layer 2 (Identifiers):** Case-insensitive substring matching on `AutomationId`, `ClassName`, and `Name` against:
   - Start: `L"Start"`, `L"StartButton"`
   - Search: `L"Search"`, `L"SearchButton"`, `L"SearchHost"`, `L"SearchBox"`
   - System: `L"TaskView"`, `L"TaskViewButton"`, `L"Widgets"`, `L"Weather"`, `L"People"`, `L"InputIndicator"`
   - Notifications: `L"Notification"`, `L"Notify"`, `L"Clock"`, `L"Tray"`
3. **Layer 3 (Lineage):** Walk ancestor tree up to 3 levels using `IUIAutomationTreeWalker`. If any ancestor has class name `TrayNotifyWnd`, `Windows.UI.Composition.DesktopWindowContentBridge` inside a secondary tray, or automation ID `SystemTrayIcon`, classify as `TE_ELEM_SYSTEM_TRAY`.
4. **Layer 4 (Image Child Evidence):** Element must possess an `Image` control child OR have an automation ID indicating a running/pinned app (e.g. `AppID:` prefix).
5. **Layer 5 (Dimensional Sanity):**
   - $16 \le W_{\text{btn}} \le 200$
   - $16 \le H_{\text{btn}} \le 200$
   - $0.4 \le \frac{W_{\text{btn}}}{H_{\text{btn}}} \le 2.5$

**Fail-Closed Policy:** Any element classified as `SHELL_CONTROL`, `SYSTEM_TRAY`, or `UNKNOWN` is immediately dropped and never added to `TE_IconElementCache`.

---

## 5. Headroom Constant & Allocation Rationale

### 5.1 Analysis of the 64px Literal
In V1, `headroom_y` was computed as `(int)(64.0f * (float)dpi / 96.0f)`.
Search across the codebase confirmed that `64` is an un-named scalar literal.

### 5.2 Geometric Requirement
For an icon of height $H_{\text{btn}}$ expanding at peak scale $S_{\text{max}} = 2.0$ with baseline anchor $c_y \le H_{\text{btn}}$, the maximum upward excursion $\Delta Y_{\text{up}}$ into desktop space is:
$$\Delta Y_{\text{up}} = c_y \cdot (S_{\text{max}} - 1.0) \le 48\text{ px} \times (2.0 - 1.0) = 48\text{ px (at 96 DPI)}$$
Adding a 33% safety boundary for subpixel antialiasing and displacement curvature gives:
$$48\text{ px} \times 1.333 = 64\text{ px}$$

### 5.3 Formal Definition
`Modules/icon_hover/icon_hover_internal.h` defines:
```c
/**
 * Baseline vertical headroom in logical pixels at 96 DPI.
 * Accommodates upward expansion for icons up to 48px base height
 * at max_scale 2.0x, with antialiasing clearance.
 * Scaled at runtime via: (TE_HOVER_HEADROOM_BASE_PX * dpi) / 96
 */
#define TE_HOVER_HEADROOM_BASE_PX 64
```

---

## 6. Frame Loop & Math Coordinate Invariance Audit

### 6.1 Audit Matrix

| File | Subroutine | Coordinate Input | Invariance Analysis | V2 Status |
|---|---|---|---|---|
| `magnification.c` | `TE_MagnifyComputeScales` | `cursor_x`, `icon_centers_x` | Computes $d = \|x_{\text{cur}} - x_{\text{icon}}\|$. Since both are SCREEN coordinates, translation offset $X_0$ cancels out: $\|(x_{\text{cur}}+X_0) - (x_{\text{icon}}+X_0)\| = \|x_{\text{cur}} - x_{\text{icon}}\|$. | **APPROVED NO CHANGE** |
| `magnification.c` | `TE_MagnifyComputeDisplacements` | `cursor_x`, `icon_centers_x`, `base_width` | Uses normalized distance $u = d / \text{radius}$ and direction sign $(dx > 0)$. Independent of origin. Base width controls displacement magnitude. | **APPROVED NO CHANGE** |
| `frame_loop.cpp` | `ComputeDisplacedPositions` | `anim[i].center_x`, `anim[i].base_width` | Consumes screen centers and outputs horizontal offsets `pos_x[]`. Hardcodes `pos_y[] = 0.0f` which correctly preserves baseline invariance. | **APPROVED NO CHANGE** |
| `frame_loop.cpp` | `RenderThreadProc` | `state->taskbar_rect`, `state->headroom_y` | Accesses taskbar bounds for mouse exit bounds checking. | **REQUIRES TRIVIAL FIELD UPDATE** (`state->geometry.taskbarRect`) |
| `frame_loop.h` | `TE_SpringStep`, `TE_CursorFilterStep` | $\Delta t$, scalar positions | Pure physics integration; independent of coordinate frame. | **APPROVED NO CHANGE** |

---

## 7. Generation Consistency & Invalidation Protocol

To guarantee that DirectComposition never renders a frame with mixed coordinate generations (e.g. an old bridge offset combined with new button bounds):

### 7.1 Monotonic Generation Counter
- `TE_TaskbarGeometryInfo` contains `uint64_t generation`.
- Initialized to `1` on first build; monotonically incremented on every geometry mutation.

### 7.2 Cache Validation Rule
Every item in `TE_IconAnimState` tracks `uint64_t geometry_generation`.
Before the render loop updates transforms:
```cpp
if (g_hover_state.anim[i].geometry_generation != g_hover_state.geometry.generation) {
    // Drop frame or clamp to rest (scale = 1.0f, opacity = 0.0f)
}
```

### 7.3 Mid-Animation Invalidation Protocol
If a geometry change (resize, DPI change, display change) occurs while icons are actively magnified:
1. `TE_FrameLoopStop()` synchronously stops and joins the render worker thread.
2. DirectComposition root opacity is set to `0.0f` and committed, hiding any floating artifacts instantly.
3. `RebuildGeometry()` is executed, generating $G_{k+1}$.
4. `RebuildIconData()` discovers fresh elements and assigns $G_{k+1}$ to all visual nodes.
5. Settle springs are snapped to rest ($S = 1.0\text{f}, v = 0.0\text{f}$).
6. `TE_FrameLoopStart()` is restarted in `TE_FRAME_STATE_IDLE`.

---

## 8. Resize Transition Lifecycle (48px -> 32px)

When `taskbar_resize` modifies taskbar dimensions, the transition follows a strict 8-stage sequence:

```
[taskbar_resize Module]
  │ (1) Hooks WM_WINDOWPOSCHANGING -> clamps Shell_TrayWnd cy = 32px
  │ (2) Shifts DesktopWindowContentBridge y_offset = (32 - 48)/2 = -8px
  │ (3) Publishes "taskbar_resize.height" = 32 into State Store
  ▼
[Windows OS / Shell_TrayWnd]
  │ (4) Emits WM_WINDOWPOSCHANGED to Shell_TrayWnd
  ▼
[Core Subclass: taskbar_subclass.c]
  │ (5) Intercepts WM_WINDOWPOSCHANGED -> fires TE_EVENT_TASKBAR_GEOMETRY
  ▼
[Icon Hover Module: icon_hover.c]
  │ (6) OnTaskbarGeometry() invoked:
  │     a. Calls TE_FrameLoopStop()
  │     b. RebuildGeometry():
  │        - Reads taskbarRect via GetWindowRect(Shell_TrayWnd)
  │        - Finds DesktopWindowContentBridge child via EnumChildWindows
  │        - Reads bridgeRect via GetWindowRect(bridge)
  │        - Calculates bridgeOffsetY = bridgeRect.top - taskbarRect.top (-8px)
  │        - Queries taskbar_resize.height from state store (32px)
  │        - Calculates baselineY = taskbarRect.bottom
  │        - Calculates headroom_y = (64 * dpi) / 96
  │        - generation++
  │        - geometry.valid = TRUE
  │     c. Moves Overlay Window:
  │        TE_DCompMoveOverlayWindow(overlay, taskbarRect.left, 
  │                                  taskbarRect.top - headroom_y,
  │                                  taskbarWidth, taskbarHeight + headroom_y)
  │     d. RebuildIconData():
  │        - Invalidate UIA cache
  │        - Discover buttons (UIA now reflects new screen positions)
  │        - Filter to APP_ICON
  │        - Rebuild DComp visual tree with new anchorY = baseline - glyphTop
  │     e. Restores anim[i] to scale = 1.0f
  │     f. Calls TE_FrameLoopStart()
  ▼
[Clean Resized Taskbar with Zero Visual Jump]
```

---

## 9. Numerical Acceptance Test Scenarios

The test suite in `Tests/test_icon_hover.cpp` will validate these explicit numerical assertions:

### Test Case 1: Standard 96 DPI, 48px Height (Scale = 1.0 & 1.3)
- Inputs: `taskbarRect = {0, 1032, 1920, 1080}`, `buttonRect = {500, 1036, 540, 1076}`, `glyphRect = {508, 1044, 532, 1068}`, `headroom_y = 64`.
- Derived:
  - `overlayRect = {0, 968, 1920, 1080}`
  - `W_surf = 40`, `H_surf = 40`
  - `glyphCenterX_OVERLAY = 520 - 0 = 520`
  - `glyphCenterY_OVERLAY = 1056 - 968 = 88`
  - `X_visual_base = 520 - 20 = 500`
  - `Y_visual_base = 88 - 20 = 68`
  - `baseline_OVERLAY = 1080 - 968 = 112`
  - `anchorX = 20.0f`
  - `anchorY = 112 - 68 = 44.0f`
- Assertions:
  - At $S = 1.0$: Visual center on screen is $(0 + 500 + 20, 968 + 68 + 20) = (520, 1056)$ -> **Exact match with glyph center**.
  - At $S = 1.0$: Baseline point on screen is $968 + 68 + 44 = 1080$ -> **Exact match with taskbar bottom**.
  - At $S = 1.3$: Baseline point on screen is $968 + 68 + [44 + 1.3 \cdot (44 - 44)] = 1080$ -> **Stationary**.
  - At $S = 1.3$: Glyph top on screen is $968 + 68 + [44 + 1.3 \cdot (12 - 44)] = 1036 - 41.6 = 994.4$ -> **Expands upward into headroom without baseline movement**.

### Test Case 2: Compact 32px Height (-8px Bridge Offset)
- Inputs: `taskbarRect = {0, 1048, 1920, 1080}`, `buttonRect = {500, 1044, 540, 1084}`, `glyphRect = {508, 1052, 532, 1076}`, `bridgeOffsetY = -8`.
- Derived:
  - `overlayRect = {0, 984, 1920, 1080}`
  - `glyphCenterY_OVERLAY = 1064 - 984 = 80`
  - `Y_visual_base = 80 - 20 = 60`
  - `baseline_OVERLAY = 1080 - 984 = 96`
  - `anchorY = 96 - 60 = 36.0f`
- Assertions:
  - At $S = 1.0$: Baseline on screen is $984 + 60 + 36 = 1080$ -> **Exact match with taskbar bottom**.
  - At $S = 1.3$: Baseline remains strictly at $1080$ -> **No floating gap**.

### Test Case 3: High-DPI Scaling (150% DPI = 144 DPI)
- Inputs: `dpi = 144`, `headroom_y = (64 * 144) / 96 = 96px`.
- Fallback Glyph Calculation: `button = 60x60`, `targetGlyphDim = round(60 * 0.60) = 36px`. `inset = 12px`.
- Assertions:
  - Fallback produces centered 36x36 glyph with 12px margins.

---

## 10. Dependency-Ordered Implementation Phases

Implementation must proceed strictly along this directed acyclic graph:

```
[Phase 1: Header Definitions & State Model]
                 │
                 ├──► [Phase 2: UIA Classifier & Discovery]
                 │              │
                 └──► [Phase 3: DComp Overlay Visual Tree]
                                │
                                ▼
                      [Phase 4: Icon Hover Integration]
                                │
                                ▼
                      [Phase 5: Build & Test Verification]
```

### Phase 1: Header Definitions & State Model
- **Target File:** `Modules/icon_hover/icon_hover_internal.h`, `Modules/icon_hover/uia_discovery.h`
- **Actions:**
  - Define `TE_HOVER_HEADROOM_BASE_PX (64)`.
  - Define `TE_TaskbarElementType` enum.
  - Define `TE_TaskbarGeometryInfo` struct.
  - Update `TE_IconAnimState` with `geometry_generation`.
  - Update `TE_IconElementInfo` with `buttonRect`, `glyphRect`, `element_type`.
- **Exit Criteria:** Headers compile cleanly across all module translation units.

### Phase 2: UIA Classifier & Discovery
- **Target File:** `Modules/icon_hover/uia_discovery.cpp`
- **Actions:**
  - Implement 5-layer `ClassifyElement()`.
  - Implement UIA image child discovery with validation rules.
  - Implement DPI-invariant ratio fallback.
  - Filter out non-`APP_ICON` elements before caching.
- **Exit Criteria:** UIA discovery returns only legitimate application icons with verified glyph rectangles.

### Phase 3: DirectComposition Visual Tree Recalibration
- **Target File:** `Modules/icon_hover/dcomp_overlay.h`, `Modules/icon_hover/dcomp_overlay.cpp`
- **Actions:**
  - Update `TE_DCompBuildVisualTree` signature to receive baseline and overlay screen bounds.
  - Position visuals at `glyphCenter - surfaceDim / 2`.
  - Set scale anchor `SetCenterY` to `baseline_OVERLAY - Y_visual_base`.
- **Exit Criteria:** Test harnesses confirm baseline point remains stationary during scaling.

### Phase 4: Icon Hover Integration & Lifecycle
- **Target File:** `Modules/icon_hover/icon_hover.c`, `Modules/icon_hover/frame_loop.cpp`
- **Actions:**
  - Implement `RebuildGeometry()` with `DesktopWindowContentBridge` discovery.
  - Refactor `RebuildIconData()` to feed measured geometry into DComp tree.
  - Update overlay repositioning in `OnTaskbarGeometry` and `OnDpiChanged`.
  - Replace magic literal `64` with `TE_HOVER_HEADROOM_BASE_PX`.
  - Trivial field rename in `frame_loop.cpp` (`state->geometry.taskbarRect`).
- **Exit Criteria:** Clean compilation with MSVC (`/W4 /WX`) and MinGW (`-Wall -Wextra -Werror`).

### Phase 5: Build & Acceptance Verification
- **Target File:** `Tests/test_icon_hover.cpp`
- **Actions:**
  - Compile and run Catch2 test suite: `te_tests.exe [icon_hover]`.
  - Verify all numerical test cases pass.
- **Exit Criteria:** 100% assertions pass, 0 regressions.
