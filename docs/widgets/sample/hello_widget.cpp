#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <sdk/te_widget.h>

namespace {
IDWriteTextFormat* g_text_format = nullptr;
bool g_clicked = false;

HRESULT Initialize(const PluginContext*) {
    IDWriteFactory* factory = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &g_text_format);
        factory->Release();
    }
    return hr;
}
HRESULT Enable() { return S_OK; }
HRESULT Disable() { return S_OK; }
HRESULT Shutdown() {
    if (g_text_format) { g_text_format->Release(); g_text_format = nullptr; }
    return S_OK;
}
WidgetMeasurement Measure(float, float) { return { 112.0f, 96.0f, 160.0f }; }
HRESULT Render(const WidgetRenderContext* ctx) {
    if (!ctx || !ctx->d2d_context || !g_text_format) return E_POINTER;
    ID2D1SolidColorBrush* brush = nullptr;
    HRESULT hr = ctx->d2d_context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush);
    if (SUCCEEDED(hr)) {
        const wchar_t* text = g_clicked ? L"Hello again!" : L"Click this widget";
        ctx->d2d_context->DrawText(text, static_cast<UINT32>(wcslen(text)), g_text_format,
            D2D1::RectF(6.0f, 0.0f, ctx->width - 6.0f, ctx->height), brush);
        brush->Release();
    }
    return hr;
}
WidgetHitTestResult HitTest(float, float) { return { true, "Click to change the message", nullptr }; }
HRESULT Click(float, float, uint32_t) { g_clicked = !g_clicked; return S_OK; }
HRESULT Scroll(float, float, float) { return S_FALSE; }
const PluginMetadata* Metadata() {
    static const PluginMetadata value{ "hello", "Hello widget", "An interactive widget example.",
        "TaskbarEngine sample", 100, 0, TE_API_VERSION };
    return &value;
}
const WidgetInterface g_interface{ sizeof(WidgetInterface), TE_WIDGET_API_VERSION,
    Initialize, Enable, Disable, Shutdown, Measure, Render, HitTest, Click, Scroll, Metadata, nullptr };
}

extern "C" TE_EXPORT const WidgetInterface* GetWidgetInterface(void) { return &g_interface; }
