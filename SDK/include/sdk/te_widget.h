#pragma once

#include <sdk/te_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_WIDGET_API_VERSION 2u

typedef struct WidgetMeasurement {
    /* Width values are device independent pixels (DIPs). */
    float preferred_width;
    float min_width;
    float max_width;
} WidgetMeasurement;

/* Graphics pointers are borrowed for the duration of Render. The host owns the
 * D2D device context and DirectComposition surface. Dimensions and input are DIPs. */
typedef struct WidgetRenderContext {
    struct ID2D1DeviceContext* d2d_context;
    float width;
    float height;
    float dpi_scale;
} WidgetRenderContext;

typedef struct WidgetHitTestResult {
    bool handled;
    const char* tooltip;
    HCURSOR cursor;
} WidgetHitTestResult;

typedef struct WidgetInterface {
    uint32_t struct_size;
    uint32_t api_version;
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
    /* Optional cheap poll used by the host to avoid repainting unchanged content. */
    bool (*IsDirty)(void);
} WidgetInterface;

typedef const WidgetInterface* (*GetWidgetInterfaceFunc)(void);

TE_EXPORT const WidgetInterface* GetWidgetInterface(void);

#ifdef __cplusplus
}
#endif
