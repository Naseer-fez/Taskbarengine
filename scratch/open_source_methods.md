# Windows 11 Taskbar Manipulation: Open-Source Methods

This document investigates how popular open-source tools manipulate the Windows 11 Taskbar and evaluates methods for dynamically modifying taskbar icon scales and widths.

## 1. ExplorerPatcher
**Mechanism**: DLL Injection, API Hooking (MinHook), and Memory Patching.
- **How it works**: ExplorerPatcher acts as a proxy DLL (usually masquerading as a system DLL like `dxgi.dll` or similar) to inject itself into `explorer.exe`. It uses MinHook to intercept and redirect Windows API calls and COM interface methods.
- **Taskbar manipulation**: In Windows 11, it often bypasses the new XAML taskbar entirely by patching memory to force `explorer.exe` to load the legacy Windows 10 taskbar code that still exists in the OS. For other tweaks, it heavily relies on hooking undocumented functions within `twinui.pcshell.dll` and `explorer.exe`.

## 2. Windhawk Mods (e.g., Taskbar Labels, Taskbar Height)
**Mechanism**: Centralized DLL Injection and Inline Hooking (via MinHook).
- **How it works**: Windhawk runs a background service that injects its core engine into target processes (`explorer.exe`). Mods are essentially C++ snippets compiled into DLLs that use Windhawk's API to install hooks.
- **Taskbar manipulation**: To modify Windows 11 taskbar items (like labels or height), mods hook into `taskbar.dll` or `Windows.UI.Xaml.dll`. They intercept specific undocumented C++ methods or WinRT/XAML layout calculation functions. For example, they might hook the function that calculates the width of a taskbar button and force it to return a larger value to accommodate text labels.

## 3. TranslucentTB
**Mechanism**: Win32 Window Manipulation and Undocumented APIs.
- **How it works**: TranslucentTB primarily relies on the undocumented Win32 API `SetWindowCompositionAttribute` (using `ACCENT_POLICY`).
- **Taskbar manipulation**: It finds the taskbar's main window handle (`Shell_TrayWnd` and `SecondaryTrayWnd`) using `FindWindow`. It then applies different composition attributes (blur, acrylic, transparent) based on desktop states (maximized windows, start menu open). It uses UIAutomation and Win32 Event Hooks (`SetWinEventHook`) to track window states and react dynamically. It avoids code injection.

## 4. RoundedTB
**Mechanism**: Win32 Region APIs and UIAutomation.
- **How it works**: RoundedTB uses standard Win32 APIs like `SetWindowRgn` to physically crop the taskbar window into a rounded shape.
- **Taskbar manipulation**: It locates the `Shell_TrayWnd` and calculates a rounded rectangle based on user settings. It heavily uses UIAutomation to monitor the dynamic resizing of the system tray (e.g., when the tray overflow menu opens or icons are added/removed) so it can recalculate and reapply the window region in real-time.

---

## Evaluating Methods for Dynamically Modifying Icon Scales and Widths

To change native XAML-based taskbar icon scales and widths on Windows 11, you must manipulate the XAML visual tree of the taskbar.

### Option A: UIAutomation (Out-of-process)
- **Pros**: Completely safe, no injection required. 
- **Cons**: UIAutomation is primarily for reading state or triggering actions. It does not allow you to modify arbitrary XAML dependency properties (like `Width`, `Height`, or `RenderTransform` for scaling) unless the developer explicitly exposed an automation pattern for it, which Microsoft did not do for taskbar icons.
- **Verdict**: Insufficient for modifying visual scales and widths.

### Option B: Memory Patching / Inline Hooking (Windhawk / ExplorerPatcher style)
- **Pros**: Extremely powerful. Can intercept the exact functions where Windows calculates layout sizes.
- **Cons**: Highly fragile and crash-prone. Any minor Windows update (Patch Tuesday) can shift memory offsets or change function signatures, causing `explorer.exe` to crash loop.

### Option C: WinRT/XAML Visual Tree Traversal (In-process DLL Injection)
- **Mechanism**: Inject a custom DLL into `explorer.exe`. Use the native WinRT XAML hosting APIs (`Windows.UI.Xaml.Media.VisualTreeHelper`) to walk the live XAML visual tree of the taskbar. Locate the specific `Grid` or `Image` elements representing the icons. Modify their XAML `DependencyProperties` directly (e.g., setting `Width`, `Height`, or a `ScaleTransform`).
- **Pros**: 
  - **Robustness**: The XAML visual tree hierarchy is much more stable across Windows updates than raw memory offsets. Even if internal functions change, the logical tree of UI elements (a button containing an icon) rarely changes structure.
  - **Flexibility**: Allows smooth, dynamic manipulation of the UI using native XAML properties.
- **Cons**: Requires DLL injection into `explorer.exe`, which can trigger antivirus software and requires careful lifecycle management to un-inject safely.

### Conclusion

The **most robust and least crash-prone method** for dynamically modifying native taskbar icon scales and widths is **Option C: WinRT/XAML Visual Tree Traversal via DLL Injection**. 

While DLL injection carries some inherent risk, manipulating the XAML visual tree using documented WinRT/XAML APIs (like `VisualTreeHelper` and `DependencyObject.SetValue`) is vastly safer than inline assembly hooking or memory patching. It avoids hardcoded offsets and relies on the relatively stable UI structure of the taskbar components.
