#include "widget_draw.h"
#include <dynamic_island_media.h>
#include <dynamic_island_media_types.h>
#include <gsmtc_source.h>
#include <memory>
#include <string>

namespace {
const PluginContext* g_context = nullptr;
std::unique_ptr<IMediaSource> g_source;
IDWriteTextFormat* g_title_format = nullptr;
IDWriteTextFormat* g_artist_format = nullptr;
TEMediaStateSnapshot g_snapshot{};
bool g_show_artist = true;
float g_max_width = 200.0f;
float g_rendered_width = 180.0f;
uint32_t g_color = 0xffffffffu;
std::wstring g_title;
std::wstring g_artist;

void LogMediaSourceFailure(const char* reason) {
    if (g_context && g_context->log) {
        g_context->log(TE_LOG_WARNING, "MediaWidget", reason);
    }
}

HRESULT Initialize(const PluginContext* context) {
    g_context = context;
    const cJSON* config = WidgetSection(context, "media");
    g_show_artist = WidgetBool(config, "show_artist", true);
    g_max_width = std::clamp(WidgetFloat(config, "max_width", 200.0f), 120.0f, 400.0f);
    g_color = WidgetTextColor(config);
    const std::string family = WidgetString(config, "font_family", "Segoe UI Variable");
    const float font_size = std::clamp(WidgetFloat(config, "font_size", 12.0f), 8.0f, 32.0f);
    HRESULT hr = WidgetCreateTextFormat(family, font_size, DWRITE_FONT_WEIGHT_NORMAL, &g_title_format);
    if (SUCCEEDED(hr) && g_show_artist) hr = WidgetCreateTextFormat(family, 10.0f, DWRITE_FONT_WEIGHT_NORMAL, &g_artist_format);
    g_source = CreateGsmtcMediaSource();
    if (!g_source) {
        LogMediaSourceFailure("Could not create the GSMTC media source");
    } else if (!g_source->Initialize()) {
        LogMediaSourceFailure("GSMTC media source initialization failed; media controls are unavailable");
        g_source.reset();
    }
    return hr;
}
HRESULT Enable() { return S_OK; }
HRESULT Disable() { return S_OK; }
HRESULT Shutdown() {
    if (g_source) { g_source->Shutdown(); g_source.reset(); }
    if (g_title_format) { g_title_format->Release(); g_title_format = nullptr; }
    if (g_artist_format) { g_artist_format->Release(); g_artist_format = nullptr; }
    g_context = nullptr;
    return S_OK;
}
WidgetMeasurement Measure(float height, float dpi) { (void)height; (void)dpi; return { std::min(g_max_width, 180.0f), 120.0f, g_max_width }; }
HRESULT Render(const WidgetRenderContext* context) {
    if (g_source && g_source->HasNewState()) g_source->GetCurrentSnapshot(&g_snapshot);
    if (g_snapshot.has_media) {
        g_title = g_snapshot.title;
        g_artist = g_snapshot.artist;
    } else {
        g_title = L"No media";
        g_artist.clear();
    }
    const float width = context ? context->width : g_max_width;
    const float height = context ? context->height : 40.0f;
    g_rendered_width = width;
    HRESULT hr = S_OK;
    const bool compact = height < 36.0f;
    if (SUCCEEDED(hr)) hr = WidgetDrawText(context, g_title_format, g_title, g_color, width,
                                           compact ? height : (g_show_artist ? 23.0f : height));
    if (SUCCEEDED(hr) && !compact && g_show_artist && g_artist_format && !g_artist.empty()) {
        hr = WidgetDrawText(context, g_artist_format, g_artist, g_color, width, 20.0f);
    }
    return hr;
}
bool IsDirty() { return g_source && g_source->HasNewState(); }
WidgetHitTestResult HitTest(float x, float y) {
    (void)y;
    return { true, x < g_rendered_width / 3.0f ? "Previous track" :
                   (x >= g_rendered_width * 2.0f / 3.0f ? "Next track" : "Play or pause"), nullptr };
}
HRESULT Click(float x, float y, uint32_t button) {
    (void)y;
    if (!g_source) {
        LogMediaSourceFailure("Media control clicked while GSMTC source is unavailable");
        return S_FALSE;
    }
    const char* action = "toggle play/pause";
    if (button == 1 || x >= g_rendered_width * 2.0f / 3.0f) {
        action = "next track";
        g_source->NextAsync();
    } else if (x < g_rendered_width / 3.0f) {
        action = "previous track";
        g_source->PreviousAsync();
    } else {
        g_source->TogglePlayPauseAsync();
    }
    if (g_context && g_context->log) g_context->log(TE_LOG_INFO, "MediaWidget", action);
    return S_OK;
}
HRESULT Scroll(float x, float y, float delta) { (void)x; (void)y; (void)delta; return S_FALSE; }
const PluginMetadata* Metadata() { static const PluginMetadata value{ "media", "Media", "Shows the current GSMTC track and playback controls.", "TaskbarEngine", 100, 0, TE_API_VERSION }; return &value; }
const PluginSettings* Settings() { return nullptr; }
const WidgetInterface g_api{ sizeof(WidgetInterface), TE_WIDGET_API_VERSION, Initialize, Enable, Disable, Shutdown, Measure, Render, HitTest, Click, Scroll, Metadata, Settings, IsDirty };
}

extern "C" TE_EXPORT const WidgetInterface* GetWidgetInterface(void) { return &g_api; }
