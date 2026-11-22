#include "widget_draw.h"
#include <cstdio>

namespace {
const PluginContext* g_context = nullptr;
IDWriteTextFormat* g_format = nullptr;
FILETIME g_prev_idle{}, g_prev_kernel{}, g_prev_user{};
uint64_t g_last_sample = 0;
float g_cpu = 0.0f;
float g_ram = 0.0f;
float g_rendered_cpu = -1.0f;
float g_rendered_ram = -1.0f;
uint32_t g_interval_ms = 2000;
bool g_show_cpu = true, g_show_ram = true;
uint32_t g_color = 0xffffffffu;
uint32_t g_bar_color = 0xff4caf50u;
uint32_t g_warning_color = 0xffff9800u;
uint32_t g_critical_color = 0xfff44336u;
float g_warning_threshold = 75.0f;
float g_critical_threshold = 90.0f;
std::string g_display_mode = "text";

uint64_t FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER number{}; number.LowPart = value.dwLowDateTime; number.HighPart = value.dwHighDateTime; return number.QuadPart;
}
void Sample() {
    const uint64_t now = GetTickCount64();
    if (g_last_sample && now - g_last_sample < g_interval_ms) return;
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const uint64_t idle_delta = FileTimeValue(idle) - FileTimeValue(g_prev_idle);
        const uint64_t kernel_delta = FileTimeValue(kernel) - FileTimeValue(g_prev_kernel);
        const uint64_t user_delta = FileTimeValue(user) - FileTimeValue(g_prev_user);
        const uint64_t total = kernel_delta + user_delta;
        g_cpu = total ? static_cast<float>(100.0 * (1.0 - static_cast<double>(idle_delta) / static_cast<double>(total))) : 0.0f;
        g_prev_idle = idle; g_prev_kernel = kernel; g_prev_user = user;
    }
    MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys) {
        g_ram = static_cast<float>(100.0 * static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys) /
                                   static_cast<double>(memory.ullTotalPhys));
    }
    g_last_sample = now;
}
HRESULT Initialize(const PluginContext* context) {
    g_context = context;
    const cJSON* config = WidgetSection(context, "sysmon");
    g_interval_ms = static_cast<uint32_t>(std::clamp(WidgetFloat(config, "update_interval_ms", 2000.0f), 1000.0f, 10000.0f));
    g_show_cpu = WidgetBool(config, "show_cpu", true);
    g_show_ram = WidgetBool(config, "show_ram", true);
    g_color = WidgetTextColor(config);
    g_bar_color = WidgetColor(config, "bar_color", g_bar_color);
    g_warning_color = WidgetColor(config, "bar_warning_color", g_warning_color);
    g_critical_color = WidgetColor(config, "bar_critical_color", g_critical_color);
    g_warning_threshold = std::clamp(WidgetFloat(config, "warning_threshold", 75.0f), 0.0f, 100.0f);
    g_critical_threshold = std::clamp(WidgetFloat(config, "critical_threshold", 90.0f), g_warning_threshold, 100.0f);
    g_display_mode = WidgetString(config, "display_mode", "text");
    const std::string family = WidgetString(config, "font_family", "Segoe UI Variable");
    HRESULT hr = WidgetCreateTextFormat(family, std::clamp(WidgetFloat(config, "font_size", 12.0f), 8.0f, 32.0f), DWRITE_FONT_WEIGHT_NORMAL, &g_format);
    GetSystemTimes(&g_prev_idle, &g_prev_kernel, &g_prev_user);
    Sample();
    return hr;
}
HRESULT Enable() { return S_OK; }
HRESULT Disable() { return S_OK; }
HRESULT Shutdown() { if (g_format) { g_format->Release(); g_format = nullptr; } g_context = nullptr; return S_OK; }
WidgetMeasurement Measure(float height, float dpi) { (void)height; (void)dpi; return { 116.0f, 88.0f, 240.0f }; }
HRESULT Render(const WidgetRenderContext* context) {
    Sample();
    const float width = context ? context->width : 116.0f;
    const float height = context ? context->height : 40.0f;
    if (context && context->d2d_context && g_display_mode != "text") {
        const float bar_width = std::max(10.0f, width - 14.0f);
        const bool compact = height < 30.0f;
        const float bar_height = compact ? 3.0f : (g_display_mode == "both" ? 5.0f : 8.0f);
        const float start_y = compact ? 3.0f : (g_display_mode == "both" ? 5.0f : 8.0f);
        ID2D1SolidColorBrush* background = nullptr;
        ID2D1SolidColorBrush* fill = nullptr;
        context->d2d_context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f), &background);
        auto draw_bar = [&](float value, float y) {
            if (!background) return;
            const D2D1_RECT_F rect = D2D1::RectF(7.0f, y, 7.0f + bar_width, y + bar_height);
            context->d2d_context->FillRectangle(rect, background);
            const uint32_t color = value >= g_critical_threshold ? g_critical_color :
                (value >= g_warning_threshold ? g_warning_color : g_bar_color);
            if (SUCCEEDED(context->d2d_context->CreateSolidColorBrush(WidgetD2DColor(color), &fill))) {
                context->d2d_context->FillRectangle(D2D1::RectF(rect.left, rect.top,
                    rect.left + bar_width * std::clamp(value / 100.0f, 0.0f, 1.0f), rect.bottom), fill);
                fill->Release(); fill = nullptr;
            }
        };
        if (g_show_cpu) draw_bar(g_cpu, start_y);
        if (g_show_ram) draw_bar(g_ram, start_y + bar_height + (compact ? 2.0f : 4.0f));
        if (background) background->Release();
        if (g_display_mode == "bar") {
            g_rendered_cpu = g_cpu;
            g_rendered_ram = g_ram;
            return S_OK;
        }
    }
    wchar_t text[96]{};
    if (g_show_cpu && g_show_ram) swprintf_s(text, L"CPU %.0f%%  |  RAM %.0f%%", g_cpu, g_ram);
    else if (g_show_cpu) swprintf_s(text, L"CPU %.0f%%", g_cpu);
    else if (g_show_ram) swprintf_s(text, L"RAM %.0f%%", g_ram);
    else swprintf_s(text, L"System");
    HRESULT hr = WidgetDrawText(context, g_format, text, g_color, width, height);
    if (SUCCEEDED(hr)) { g_rendered_cpu = g_cpu; g_rendered_ram = g_ram; }
    return hr;
}
bool IsDirty() {
    const float old_cpu = g_cpu, old_ram = g_ram;
    Sample();
    return static_cast<int>(old_cpu + 0.5f) != static_cast<int>(g_cpu + 0.5f) ||
           static_cast<int>(old_ram + 0.5f) != static_cast<int>(g_ram + 0.5f) ||
           static_cast<int>(g_rendered_cpu + 0.5f) != static_cast<int>(g_cpu + 0.5f) ||
           static_cast<int>(g_rendered_ram + 0.5f) != static_cast<int>(g_ram + 0.5f);
}
WidgetHitTestResult HitTest(float x, float y) { (void)x; (void)y; return { true, "CPU and memory usage", nullptr }; }
HRESULT Click(float x, float y, uint32_t button) { (void)x; (void)y; (void)button; return S_OK; }
HRESULT Scroll(float x, float y, float delta) { (void)x; (void)y; (void)delta; return S_FALSE; }
const PluginMetadata* Metadata() { static const PluginMetadata value{ "sysmon", "System Monitor", "Displays CPU and memory usage.", "TaskbarEngine", 100, 0, TE_API_VERSION }; return &value; }
const PluginSettings* Settings() { return nullptr; }
const WidgetInterface g_api{ sizeof(WidgetInterface), TE_WIDGET_API_VERSION, Initialize, Enable, Disable, Shutdown, Measure, Render, HitTest, Click, Scroll, Metadata, Settings, IsDirty };
}

extern "C" TE_EXPORT const WidgetInterface* GetWidgetInterface(void) { return &g_api; }
