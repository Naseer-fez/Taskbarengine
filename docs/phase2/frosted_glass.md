# Frosted Glass Transparency

## Defaults

`taskbar_transparency` starts enabled with `mode: "acrylic"`, `opacity: 0.65`, `color: "0x44000000"`, `accent_flags: 2`, and `apply_secondary: true`. The ARGB tint is converted to the ABGR layout required by the Windows accent policy; opacity scales the tint alpha.

```jsonc
"plugins": {
  "taskbar_transparency": {
    "enabled": true,
    "mode": "acrylic", // clear | blur | acrylic | tint | disabled
    "opacity": 0.65,    // 0.0–1.0
    "color": "0x44000000",
    "accent_flags": 2,
    "apply_secondary": true
  }
}
```

## Settings

- `mode` selects transparent gradient, blur-behind, acrylic, tint, or native styling.
- `opacity` and `color` tune the acrylic tint. Color is entered as ARGB; the composition API packs it for Windows.
- `accent_flags` is passed through for advanced users. Its meaning is undocumented by Windows and can vary by OS build.
- `apply_secondary` controls secondary taskbar discovery and policy application.

The module exposes these supported parameters through `SettingDescriptor`, so the Settings app builds controls from IPC metadata. `noise_intensity` and `luminosity_blend` are documented as OS-controlled. The selected accent API has no supported fields for setting them independently.

## Live Updates and Recovery

Config changes parse into the current settings and rediscover taskbars before applying the new policy. DPI and display-topology events recompute and reapply policy; topology handling also debounces a second discovery to catch taskbars Explorer recreates. Disabling the plugin restores native taskbar styling. Windows accent policy is undocumented, so exact appearance can vary with Windows version, theme, contrast settings, and system policy. Acrylic noise and luminosity blending are controlled by Windows; this API does not expose reliable independent controls for them.
