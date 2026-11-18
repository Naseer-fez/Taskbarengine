# TaskbarEngine Frosted Glass: focused research report

Date: 2026-11-17 (Asia/Calcutta)
Workspace: `D:\CODE\Utlities\Taskbar`
Research scope: source and documentation review only. No production files, settings, deployed binaries, or running installation were changed. No new test binary was built or deployed.

## Conclusion

A plausible, source-backed Windows 11 route exists: operate on the taskbar’s XAML background element in Explorer and replace its brush with a XAML Acrylic brush or a Windows Composition backdrop/blur brush. TranslucentTB’s source implements both. This targets the visual element that paints the surface, rather than assuming that an accent policy on `Shell_TrayWnd` will affect the XAML-rendered taskbar. It is an unsupported shell customization and remains version-fragile.

This research did **not** demonstrate visible blur on this machine. The machine reports Windows 11 Home Single Language, 64-bit, version 10.0.26200, build 26200. The user-provided injected-Explorer observations (root call returned success with no visible effect; bridge call failed with `GetLastError() == 6`) are evidence of accepted/root-vs-child behavior in that experiment, but not a controlled contrast-pattern test. No claim of verified pixels or “100% working” is made.

The safer supported route is an app-owned overlay HWND using Desktop Acrylic/system backdrop APIs or a custom Composition backdrop brush. It can visibly blur what lies behind the overlay, but it is not the Explorer taskbar surface: keeping it aligned, click-through, visually integrated, and correct on every monitor requires an overlay window manager. It is a reasonable fallback or prototype, not a direct styling API for Explorer’s taskbar.

## Candidate comparison

| Approach | Target and process | Visible behavior | Support and evidence | Main risks / limits |
|---|---|---|---|---|
| `SetWindowCompositionAttribute(WCA_ACCENT_POLICY)` with `ACCENT_ENABLE_ACRYLICBLURBEHIND` | Call using `user32!SetWindowCompositionAttribute` on an HWND. Common legacy taskbar utilities target `Shell_TrayWnd`; TaskbarEngine currently tries root and discovered `DesktopWindowContentBridge`. | Accent states can produce blur/acrylic/tint when the target window’s rendering path honors them. A Boolean success only means the undocumented function accepted the request; it does not prove visible blur. | Undocumented API, absent from supported Win32 contract. TranslucentTB still uses this for its classic/classic-style taskbar path: [`taskbarattributeworker.cpp`](https://github.com/TranslucentTB/TranslucentTB/blob/release/TranslucentTB/taskbar/taskbarattributeworker.cpp). Windows version behavior depends on shell implementation. | On current Windows 11 taskbar the background is a XAML visual; root HWND acceptance may leave pixels unchanged. Child HWND targeting is not a documented way to style the XAML background. Explorer may overwrite the state; accent values and visual result vary by build. User’s observed return TRUE/no visual effect demonstrates the key false-positive risk on this installation. |
| XAML Diagnostics + replace taskbar background brush (TranslucentTB pattern) | A helper loaded into `explorer.exe` uses XAML Diagnostics to inspect the taskbar XAML tree, finds taskbar frame/background `Shape` elements, then registers them with an in-process taskbar appearance service. Service runs on Explorer/XAML dispatcher thread. It changes `Shape.Fill`, not `DesktopWindowContentBridge` HWND attributes. | Source uses `AcrylicBrush` for acrylic, or a custom Composition effect whose source is `Compositor.CreateBackdropBrush()`, with Gaussian blur and a tint flood effect. This is real compositor backdrop sampling by source construction, not just an alpha tint. | Actual project source: [`taskbarappearanceservice.cpp`](https://github.com/TranslucentTB/TranslucentTB/blob/release/ExplorerTAP/taskbarappearanceservice.cpp), [`XamlBlurBrush.cpp`](https://github.com/TranslucentTB/TranslucentTB/blob/release/ExplorerTAP/XamlBlurBrush.cpp), and [`taskbarattributeworker.cpp`](https://github.com/TranslucentTB/TranslucentTB/blob/release/TranslucentTB/taskbar/taskbarattributeworker.cpp). XAML Diagnostics entry point is documented for debugging tools at [InitializeXamlDiagnosticsEx](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-initializexamldiagnosticsex); the docs do not endorse modifying Explorer’s taskbar. No stable minimum Windows version is promised for these private shell tree details. | Explorer injection and XAML Diagnostics are unsupported and sensitive to OS build, XAML tree changes, one-diagnostics-client constraints, dispatcher/lifetime mistakes, and Explorer crashes. Must rediscover elements after taskbar/XAML recreation and restore original brushes. Open-source implementation proves the technique exists, not that it works on build 26200 or future builds. |
| Supported Desktop Acrylic/system backdrop on an owned top-level window | App-owned top-level HWND + Windows App SDK `DesktopAcrylicController.SetTarget(WindowId, CompositionTarget)`, or WinUI `Window.SystemBackdrop`. For raw Win32 Composition host backdrop, set `DWMWA_USE_HOSTBACKDROPBRUSH` on the owning top-level window and render with `Compositor.CreateHostBackdropBrush`. Runs in TaskbarEngine’s own process and UI/dispatcher thread. | Actual system Acrylic backdrop, or custom backdrop brush, inside the app-owned window. Controller’s `SetTarget` returns `bool`; docs specify false means no setup and true means configured. DWM attribute acceptance alone does not prove that the app rendered a backdrop brush. | Supported documented APIs. [DesktopAcrylicController.SetTarget](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.composition.systembackdrops.desktopacryliccontroller.settarget?view=windows-app-sdk-1.8); [system backdrop guidance](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/system-backdrop-controller); [DWMWA_USE_HOSTBACKDROPBRUSH](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute). Host backdrop attribute is documented for Windows 11 build 22000+. Windows App SDK system backdrop controller APIs are available in SDK 1.x; the recommended `Window.SystemBackdrop` property is available starting with Windows App SDK 1.3. | Styles only the owned window. Overlay may cover icons/content, and transparent/click-through layering can complicate input and backdrop behavior. Needs per-monitor geometry, taskbar position/alignment, DPI/display notifications, z-order, accessibility, and Explorer restart handling. System backdrop policy may fall back or disable effects. |
| `DwmEnableBlurBehindWindow` | An HWND passed to DWM from any process, conventionally the app’s own window. `DWM_BLURBEHIND` carries enable/region/transition flags. | Not a viable modern taskbar blur solution. Though the API remains documented, Microsoft states blur-behind no longer produces the effect beginning with Windows 8. | [DwmEnableBlurBehindWindow docs](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmenableblurbehindwindow) | Deprecated behavior for this purpose; `S_OK` is not evidence of blur on Windows 8+. |
| DirectComposition custom overlay | App-owned child/overlay HWND or visual target, e.g. TaskbarEngine’s existing overlay infrastructure. Composition visual can carry custom blur/tint if backed by a supported backdrop source and suitable HWND host. | Can draw a blur/tint surface over the taskbar region, but is separate from Explorer’s own surface. Ordinary alpha/color fills are only translucency/tint, not backdrop blur. | DirectComposition is supported; TaskbarEngine already has DirectComposition overlay code in `Modules/icon_hover/dcomp_overlay.cpp`. The backdrop source must be chosen carefully; system backdrop controller or Win11 host backdrop is documented. | A child window cannot necessarily sample pixels behind its parent; overlay ordering, clipping, non-client regions, click-through, high contrast, taskbar animations, and multi-monitor placement need validation. A DirectComposition visual alone does not create a blur source. |

## What the local source establishes

`Modules/taskbar_transparency/composition_api.c` dynamically resolves the undocumented entry point and builds `WCA_ACCENT_POLICY` data with `ACCENT_ENABLE_ACRYLICBLURBEHIND`, tint color, and alpha derived from settings. `TE_ApplyAccentPolicy` returns the API’s Boolean and captures `GetLastError()` only when it returns false. A true return leaves no pixel-level proof. Its policy construction and unit tests validate the structure/packing, not taskbar rendering.

`Modules/taskbar_transparency/tray_discovery.c` discovers primary `Shell_TrayWnd`, secondary `Shell_SecondaryTrayWnd`, searches descendants for the class `Windows.UI.Composition.DesktopWindowContentBridge`, then applies the same undocumented policy to both. It logs HWND, class, PID/TID, Boolean, and error. The inspected code does not identify or edit the XAML background brush.

The current `docs/phase2/frosted_glass.md` describes those settings and the undocumented accent policy. It is useful as implementation context, not independent evidence that the current Windows 11 taskbar visibly blurs.

The user-provided experimental observations on this system are consistent with a visual-layer mismatch: root `Shell_TrayWnd` accepted the request but looked opaque; the bridge rejected it with error 6. Error 6 is `ERROR_INVALID_HANDLE`, but because the API is undocumented, this does not establish a supported child-window contract or explain why that specific target failed. Preserve this as a finding, not as a proven diagnosis.

## API and lifecycle details

### Undocumented accent policy

The common ABI uses a `WINDOWCOMPOSITIONATTRIBDATA` envelope (`Attrib = WCA_ACCENT_POLICY`, pointer to `ACCENT_POLICY`, byte size) and numeric `ACCENT_ENABLE_ACRYLICBLURBEHIND` state. It is resolved by name from User32. Microsoft does not document a supported signature, version contract, minimum OS, thread requirement, or guarantee that success means a particular rendered material. Error handling should record `GetLastError()` immediately after FALSE; a stale last-error after TRUE must not be interpreted as a meaningful HRESULT.

### Desktop Acrylic controller

Microsoft documents `DesktopAcrylicController.IsSupported`, properties (including tint, luminosity, and fallback settings in SDK versions that expose them), `SetTarget(WindowId, CompositionTarget)`, and disposal/removal of targets. `SetTarget` must run on a thread with a DispatcherQueue. For Win32, Microsoft specifically says Desktop Acrylic requires `DWMWA_USE_HOSTBACKDROPBRUSH` on the top-level HWND. A controller’s successful target setup is stronger than an opaque API Boolean but should still be checked by a contrast-pattern visual test.

### XAML/Composition backdrop brush

The TranslucentTB taskbar source sets `AcrylicBrush.BackgroundSource(Backdrop)` and tint for one mode. For its explicit blur mode, it obtains the taskbar element visual’s compositor, creates a backdrop brush, feeds it to a Gaussian blur effect, composites a tint flood, and assigns the resulting Composition brush as the XAML shape fill. This is direct evidence that the surface gets a backdrop-sampling brush. The diagnostic tree discovery and private taskbar element access remain undocumented/unsupported, and the source code alone cannot verify visible output on this machine.

## Taskbar geometry and lifecycle implications

- **Multiple monitors:** handle each taskbar independently. Windows exposes primary `Shell_TrayWnd` and secondary `Shell_SecondaryTrayWnd` in current desktop shells, but the XAML tree and class hierarchy are implementation details. Discover by ownership/monitor and verify actual bounds; do not assume one root or one bridge per display. Update each overlay’s monitor-specific bounds and DPI.
- **Position/alignment:** top/bottom/left/right placement and centered/left-aligned icons affect taskbar region and background element geometry. Brush replacement follows the taskbar element’s own layout; overlays need to listen for position, size, taskbar settings, work-area, and display changes and resize/reposition from actual HWND geometry.
- **DPI/display changes:** recompute per-monitor bounds and re-enumerate affected windows/elements on DPI and topology changes. Debouncing a rediscovery after Explorer layout completes is reasonable; avoid retaining stale HWND or XAML element references.
- **Explorer restarts/recreation:** detect Explorer process exit and `TaskbarCreated`/window create-destroy events; reacquire taskbar HWNDs, reinject/reinitialize any Explorer component, find new XAML elements, and reapply. On clean shutdown restore each captured original XAML `Fill`; tie restoration to helper process death. Never trust stored HWNDs across Explorer restart.
- **Tint and opacity:** preserve the configured ARGB tint and opacity. For a Composition/XAML brush translate channels once, clamp, and retain alpha. If using AcrylicBrush, test whether its system material blending maps acceptably to the existing opacity slider; it is not necessarily numerically equivalent to accent-policy alpha. Do not silently change user settings.
- **Rendering verification:** use a high-contrast animated pattern behind the taskbar and record before/after screenshots or pixel samples in taskbar bounds. Test both static bright/dark tiles and a changing region so a tint overlay cannot be mistaken for live blur. Log HWND/class/PID/TID, exact policy/brush parameters, API result and error/HRESULT, Windows build, display topology, and restoration result.

## Recommended design for TaskbarEngine

1. Treat direct Accent Policy as a best-effort compatibility mode only. Keep a clear unsupported status in diagnostics; report API acceptance separately from visual verification. Do not use the `DesktopWindowContentBridge` child’s error 6 as the final explanation.
2. For Windows 11, prototype a separate Explorer-side adapter that uses XAML Diagnostics to identify the taskbar background `Shape` (or corresponding current element), captures its original fill, and installs a custom backdrop blur/tint brush on the correct XAML dispatcher. This follows the closest proven open-source design. Isolate it behind build/capability checks and fail closed if tree discovery, element type, or restoration setup differs.
3. Keep an app-owned overlay as the supported fallback if Explorer integration is undesirable or unavailable. Use a top-level transparent overlay window positioned over each taskbar and a documented Desktop Acrylic or Composition host-backdrop effect. Prototype interaction carefully because full-taskbar overlays can obscure native visuals or input.
4. Add lifecycle ownership: monitor taskbar and XAML element creation/destruction, Explorer process exit, and display/DPI/work-area events; rediscover and reapply after debounce; restore the captured brush on disable and when the owning helper exits. Apply independently per monitor.
5. Preserve current color/opacity settings, but calibrate their mapping visually against the existing slider using the contrast test. Expose a warning/fallback state if Windows suppresses backdrop materials or if the visual contract changes.

This is a design recommendation, not an implementation change.

## Test setup and results

- **Host queried:** PowerShell `Get-ComputerInfo` and `Win32_OperatingSystem`.
- **OS:** Windows 11 Home Single Language, 64-bit, version `10.0.26200`, build `26200`.
- **Existing test (user-provided):** injected Explorer module called accent policy on root `Shell_TrayWnd`: API returned success, taskbar remained visually opaque. Called same policy on child `Windows.UI.Composition.DesktopWindowContentBridge`: failed, `GetLastError() == 6` (`ERROR_INVALID_HANDLE`). Tint/Acrylic settings were preserved.
- **What was not performed:** no new API test, no scratch project, no contrast-pattern run, no screenshot or pixel-diff measurement, no Explorer restart/recreation test. Therefore no API was visually verified on build 26200 by this investigation.
- **Repository integrity:** `git status --short` showed many pre-existing modified and untracked files before research authoring. They were left untouched; this report is the only requested research artifact created in this task.

## Remaining unknowns and next step

1. Whether TranslucentTB’s XAML Diagnostics taskbar brush replacement works on this exact build 26200 and current taskbar package/version.
2. Whether the user’s bridge HWND is stale, belongs to a different HWND hierarchy, or simply rejects accent policy for another undocumented reason.
3. The current exact XAML element identity/visual tree for the installed shell build, and whether it can be accessed reliably without destabilizing Explorer.
4. How closely system Acrylic tint/opacity parameters can match TaskbarEngine’s configured slider.
5. Whether an owned top-level Acrylic overlay can sample the desired behind-taskbar content while remaining click-through and aligned on every monitor.

Practical next step: build a **separate, reversible scratch prototype** under `research/` that first tests Desktop Acrylic/backdrop brushes on an app-owned window with an animated contrast pattern. If that verifies, separately investigate XAML Diagnostics against the real taskbar behind an explicit restore path, logging tree target identity and testing after taskbar recreation. Do not change production settings or code until that evidence is collected and reviewed.

## Sources

### Microsoft documentation

- [System backdrops (Mica/Acrylic), Windows App SDK](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/system-backdrop-controller)
- [DesktopAcrylicController.SetTarget](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.composition.systembackdrops.desktopacryliccontroller.settarget?view=windows-app-sdk-1.8)
- [ISystemBackdropController behavior and target return semantics](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.composition.systembackdrops.isystembackdropcontroller?view=windows-app-sdk-1.8)
- [DWMWINDOWATTRIBUTE / DWMWA_USE_HOSTBACKDROPBRUSH](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)
- [InitializeXamlDiagnosticsEx](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-initializexamldiagnosticsex)
- [DwmEnableBlurBehindWindow, including Windows 8+ behavior](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmenableblurbehindwindow)
- [DwmExtendFrameIntoClientArea](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)

### Source code reviewed

- [TranslucentTB classic/taskbar attribute worker](https://github.com/TranslucentTB/TranslucentTB/blob/release/TranslucentTB/taskbar/taskbarattributeworker.cpp)
- [TranslucentTB Explorer taskbar appearance service](https://github.com/TranslucentTB/TranslucentTB/blob/release/ExplorerTAP/taskbarappearanceservice.cpp)
- [TranslucentTB Composition blur brush](https://github.com/TranslucentTB/TranslucentTB/blob/release/ExplorerTAP/XamlBlurBrush.cpp)
- [TranslucentTB current implementation overview](https://translucenttb.dev/docs/how-it-works/)

### Workspace files inspected (read only)

- `Modules/taskbar_transparency/composition_api.c`
- `Modules/taskbar_transparency/tray_discovery.c`
- `Modules/taskbar_transparency/composition_api.h`
- `Modules/icon_hover/dcomp_overlay.cpp`
- `Modules/TaskbarPayload/XamlBridge.cpp`
- `docs/phase2/frosted_glass.md`
