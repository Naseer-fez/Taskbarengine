# TaskbarEngine widget packages

## Install a widget

Create a folder under the installed `Widgets` directory using a stable lowercase ID. Put the widget DLL and a `widget.json` manifest in that folder, then restart TaskbarEngine so the catalog and module discovery refresh. For example:

```text
TaskbarEngine/
  Widgets/
    weather/
      widget.json
      weather.dll
```

The manifest format is UTF-8 JSON:

```json
{
  "manifest_version": 1,
  "api_version": 2,
  "id": "weather",
  "name": "Weather",
  "description": "Shows local weather.",
  "module": "weather.dll",
  "settings": [
    { "key": "city", "label": "City", "type": "string", "default": "London" },
    { "key": "show_temperature", "label": "Show temperature", "type": "bool", "default": true }
  ]
}
```

`id` must be unique and contain only lowercase ASCII letters, digits, `.`, `_`, and `-` (maximum 64 characters). `module` must be a DLL filename in the same package folder; directory separators and `..` are rejected. The module must export `GetWidgetInterface`, and the API version and ID must match the manifest. The Settings page reads declared settings from installed manifests and stores them under `widgets.<id>`. Supported setting types are `bool`, `int`, `float`, `string`, `enum`, and `color`; numeric settings may provide `min`, `max`, and `step`, and enum settings provide `options`.

Build the DLL for the same architecture as TaskbarEngine and implement the version 2 `WidgetInterface` in `SDK/include/sdk/te_widget.h`. Measurement and pointer coordinates use DIPs. The host owns the Direct2D context and borrows it for the duration of `Render`. A widget should keep callbacks short and avoid retaining borrowed graphics pointers. The optional `IsDirty` callback must be a cheap poll: return true only when `Render` would produce changed content. The host keeps legacy v2 modules without this callback working by repainting them on its normal one-second update.

The interactive example is in `docs/widgets/sample`. From a Visual Studio developer shell, build it with:

```powershell
cmake -S docs/widgets/sample -B out/hello-widget -DTASKBARENGINE_SDK="D:/path/to/TaskbarEngine/SDK"
cmake --build out/hello-widget --config Release
New-Item -ItemType Directory -Force "D:/path/to/TaskbarEngine/Widgets/hello"
Copy-Item out/hello-widget/Release/hello.dll "D:/path/to/TaskbarEngine/Widgets/hello/hello.dll"
Copy-Item docs/widgets/sample/widget.json "D:/path/to/TaskbarEngine/Widgets/hello/widget.json"
```

After restart, enable **Hello widget** in Settings and move it into the preferred order. Click it to change its text; this exercises the hit-test and click callbacks.

The built-in packages under `Widgets/clock`, `Widgets/sysmon`, and `Widgets/media` are reference manifests. To remove a widget, stop TaskbarEngine, remove its package directory, and start TaskbarEngine again. Its saved settings are retained so reinstalling the same ID restores them.

## Current limitations

This package/catalog and API validation path is in place, but widget callbacks still run in the engine plugin process and rendering still uses the legacy shared overlay integration. Native taskbar placement is a best-effort compatibility path tied to Explorer's taskbar and the `icon_hover` shared overlay; it is not a documented Windows taskbar extension point. The planned app-owned adjacent bar, per-widget worker processes, worker supervision, Explorer-independent runtime recovery, and runtime acceptance evidence are not implemented. Only install widgets you trust while running this version. The manifest/API version checks do not provide process isolation.
