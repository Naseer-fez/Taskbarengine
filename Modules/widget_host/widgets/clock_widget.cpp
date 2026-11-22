#include "widget_draw.h"
#include <ctime>
#include <string>

namespace {
const PluginContext* g_context = nullptr;
IDWriteTextFormat* g_format = nullptr;
IDWriteTextFormat* g_date_format = nullptr;
std::wstring g_time_pattern = L"HH:mm";
std::wstring g_date_pattern = L"ddd, MMM d";
bool g_show_date = false;
float g_font_size = 13.0f;
uint32_t g_color = 0xffffffffu;
float g_width = 64.0f;
std::wstring g_last_rendered;

bool ConvertFormat(const std::string& source, std::wstring& output) {
    output = WidgetWide(source);
    return !output.empty();
}

std::wstring CurrentTime(const std::wstring& pattern, bool date) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t buffer[128]{};
    const int count = date ? GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, pattern.c_str(), buffer, 128, nullptr)
                           : GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, pattern.c_str(), buffer, 128);
    return count > 0 ? std::wstring(buffer) : std::wstring();
}

HRESULT Initialize(const PluginContext* context) {
    g_context = context;
    const cJSON* config = WidgetSection(context, "clock");
    ConvertFormat(WidgetString(config, "format", "HH:mm"), g_time_pattern);
    ConvertFormat(WidgetString(config, "date_format", "ddd, MMM d"), g_date_pattern);
    g_show_date = WidgetBool(config, "show_date", false);
    g_font_size = std::clamp(WidgetFloat(config, "font_size", 13.0f), 8.0f, 32.0f);
    g_color = WidgetTextColor(config);
    g_width = std::clamp(static_cast<float>(g_time_pattern.size()) * g_font_size * 0.62f + 24.0f,
                         64.0f, 220.0f);
    if (g_show_date) g_width = std::clamp(g_width + 70.0f, 120.0f, 240.0f);
    const std::string family = WidgetString(config, "font_family", "Segoe UI Variable");
    HRESULT hr = WidgetCreateTextFormat(family, g_font_size, DWRITE_FONT_WEIGHT_NORMAL, &g_format);
    if (SUCCEEDED(hr) && g_show_date) hr = WidgetCreateTextFormat(family, g_font_size - 2.0f, DWRITE_FONT_WEIGHT_NORMAL, &g_date_format);
    return hr;
}
HRESULT Enable() { return S_OK; }
HRESULT Disable() { return S_OK; }
HRESULT Shutdown() {
    if (g_format) { g_format->Release(); g_format = nullptr; }
    if (g_date_format) { g_date_format->Release(); g_date_format = nullptr; }
    g_context = nullptr;
    return S_OK;
}
WidgetMeasurement Measure(float height, float dpi) {
    (void)dpi;
    (void)height;
    (void)dpi;
    return { g_width, g_width, g_width };
}
HRESULT Render(const WidgetRenderContext* context) {
    std::wstring text = CurrentTime(g_time_pattern, false);
    if (g_show_date) text += L"  " + CurrentTime(g_date_pattern, true);
    HRESULT hr = WidgetDrawText(context, g_format, text, g_color, context ? context->width : g_width,
                                context ? context->height : 40.0f);
    if (SUCCEEDED(hr)) g_last_rendered = text;
    return hr;
}
bool IsDirty() {
    std::wstring text = CurrentTime(g_time_pattern, false);
    if (g_show_date) text += L"  " + CurrentTime(g_date_pattern, true);
    return text != g_last_rendered;
}
WidgetHitTestResult HitTest(float x, float y) {
    (void)x; (void)y;
    return { true, "Current time", nullptr };
}
HRESULT Click(float x, float y, uint32_t button) { (void)x; (void)y; (void)button; return S_OK; }
HRESULT Scroll(float x, float y, float delta) { (void)x; (void)y; (void)delta; return S_FALSE; }
const PluginMetadata* Metadata() {
    static const PluginMetadata value{ "clock", "Clock", "Displays the current time and optional date.", "TaskbarEngine", 100, 0, TE_API_VERSION };
    return &value;
}
const PluginSettings* Settings() { return nullptr; }
const WidgetInterface g_api{ sizeof(WidgetInterface), TE_WIDGET_API_VERSION, Initialize, Enable, Disable, Shutdown, Measure, Render, HitTest, Click, Scroll, Metadata, Settings, IsDirty };
}

extern "C" TE_EXPORT const WidgetInterface* GetWidgetInterface(void) { return &g_api; }
