# Widget Host Architecture

## Hosting and Graphics

`widget_host.dll` scans the installation's `Widgets/` directory for DLLs exporting `GetWidgetInterface`. Each loaded widget receives its own `IDCompositionSurface`, `IDCompositionVisual`, and `ID2D1DeviceContext`. The surface is drawn by beginning a DComp surface update, wrapping its DXGI surface as a D2D target bitmap, invoking the widget renderer, and ending the surface update.

Graphics resources are shared with the engine overlay. The overlay owns the D3D/D2D/DComp devices and primary composition target. Its root contains independent icon and widget layers, with existing overlay content attached through its established layer. Icon-hover opacity animations affect only the icon layer, keeping widgets visible at rest. Widgets are positioned relative to the primary taskbar rectangle and current DPI.

## Lifecycle and Layout

The host loads widget DLLs during initialization, probes their interface and metadata, and initializes them with a borrowed `PluginContext`. Enable creates surfaces for configured widgets. Disable stops timers, removes the taskbar subclass, detaches visuals, and releases contexts and surfaces. Shutdown calls widget shutdown and unloads each DLL.

Widget `Measure` results are clamped to each widget's minimum and maximum widths. Layout order comes from `widgets.layout`; the default alignment zone is selected by `widgets.position`, with `widgets.gap` between cards. Mouse input is received by a taskbar subclass because the visual overlay is click-through. Hit testing is delegated to the widget under the pointer. Dragging updates the target order and applies a critically damped spring to visual offsets; drop writes the order back to config.

## Events and Failure Handling

Config events for the top-level `widgets` section rebuild widget settings and surfaces. DPI and display changes refresh geometry. The host uses a one-second UI timer for clock rendering and spring animation; system monitoring samples at its configured interval. GSMTC session changes are sourced by the existing event-driven media source. This widget host currently attaches to the primary taskbar only.

Widget entry points are wrapped in SEH. A widget callback that raises a structured exception is marked faulted and skipped while other widgets continue. This is best-effort in-process containment: it does not protect Explorer from hangs or all memory-corruption failures. DComp device loss must trigger recreation of shared resources and widget surfaces before drawing resumes.

## Configuration and Settings

The engine sends top-level `widgets` changes as `TE_EVENT_CONFIG_CHANGED` with `plugin_name: "widgets"`; the widget host receives that subtree. Its `GetSettings` descriptors are serialized by the existing Settings IPC. The Settings app maps host controls into `widgets`, including conversion of the order text control to the persisted JSON array. Runtime drag reorder writes that same `layout` array.

## Performance

No widget busy-spins. Clock and monitoring use bounded timers, GSMTC uses its event source, rendering reuses surfaces and contexts, and the spring loop is idle outside active animation. The budget is less than 0.5% CPU and 15 MB additional working set at idle; validate with an Explorer session and representative multi-monitor configurations.
