# Widget API Specification

## ABI

Include `sdk/te_widget.h` and export an unmangled C factory:

```c
#include <sdk/te_widget.h>

TE_EXPORT const WidgetInterface* GetWidgetInterface(void);
```

The ABI is C17 and uses the same calling and ownership rules as `PluginInterface`. Do not expose C++ classes, STL objects, exceptions, or compiler-owned strings. Graphics pointers are opaque COM interface pointers in the public header. A renderer may use C++ internally, but exceptions must not cross the ABI boundary.

```c
typedef struct WidgetMeasurement {
    float preferred_width;
    float min_width;
    float max_width;
} WidgetMeasurement;

typedef struct WidgetRenderContext {
    struct ID2D1DeviceContext* d2d_context; /* borrowed for Render */
    float width;       /* allocated DIPs */
    float height;      /* allocated DIPs */
    float dpi_scale;
} WidgetRenderContext;

typedef struct WidgetHitTestResult {
    bool handled;
    const char* tooltip; /* optional UTF-8 text */
    HCURSOR cursor;      /* optional; NULL selects the default */
} WidgetHitTestResult;

typedef struct WidgetInterface {
    HRESULT (*Initialize)(const PluginContext* ctx);
    HRESULT (*Enable)(void);
    HRESULT (*Disable)(void);
    HRESULT (*Shutdown)(void);
    WidgetMeasurement (*Measure)(float available_height, float dpi_scale);
    HRESULT (*Render)(const WidgetRenderContext* ctx);
    WidgetHitTestResult (*HitTest)(float x, float y);
    HRESULT (*OnClick)(float x, float y, uint32_t button);
    HRESULT (*OnScroll)(float x, float y, float delta);
    const PluginMetadata* (*GetMetadata)(void);
    const PluginSettings* (*GetSettings)(void);
} WidgetInterface;
```

The host currently uses click button values `0=left` and `1=right`. Coordinates passed to interaction callbacks are widget-local DIPs. `HitTest` returns a tooltip pointer valid until the widget's next callback; the host copies it before returning to the message loop.

## Lifecycle and Ownership

`Initialize` runs once after loading. The supplied context is borrowed; copy only data needed after the call. `Enable` starts event/timer work. `Disable` stops it and releases transient state while allowing a later re-enable. `Shutdown` releases all widget-owned memory and COM references. `Measure` returns preferred/min/max width in DIPs. The host owns the surface and D2D context; widget renderers must not retain the render context or release its pointer.

All callbacks are guarded with SEH by the host. On a structured fault, that widget is disabled for the session. This cannot contain a hang or every form of heap corruption because widgets share Explorer's process.

## Minimal Example

```c
static HRESULT RenderClock(const WidgetRenderContext* ctx) {
    /* Draw with ctx->d2d_context; do not retain the context. */
    return S_OK;
}

static WidgetMeasurement MeasureClock(float height, float dpi) {
    (void)height; (void)dpi;
    return (WidgetMeasurement){ 80.0f, 48.0f, 140.0f };
}

static const PluginMetadata metadata = {
    "clock", "Clock", "Displays local time", "Example", 100, 0, TE_API_VERSION
};
static const PluginMetadata* GetMetadata(void) { return &metadata; }
static const WidgetInterface api = {
    NULL, NULL, NULL, NULL, MeasureClock, RenderClock, NULL, NULL, NULL, GetMetadata, NULL
};
TE_EXPORT const WidgetInterface* GetWidgetInterface(void) { return &api; }
```

Build each widget as its own DLL and place it under `Widgets/`. The host ignores DLLs with a missing factory, invalid metadata, or incomplete required callbacks. Callback order and metadata strings must remain valid for the lifetime of the loaded DLL.
