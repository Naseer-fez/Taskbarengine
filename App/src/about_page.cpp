#include "about_page.h"
#include "gui_ipc_client.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Windows.UI.Text.h>
#include <cJSON.h>
#include <cstdio>
#include <string>

#ifndef TE_VERSION_STRING
#define TE_VERSION_STRING "1.0.0"
#endif

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::TaskbarEngine {

Page CreateAboutPage()
{
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(16);
    panel.Padding(Thickness{ 24, 24, 24, 24 });
    
    TextBlock title;
    title.Text(L"About TaskbarEngine");
    title.Style(Application::Current().Resources().Lookup(box_value(L"TitleTextBlockStyle")).as<Style>());
    title.Margin(Thickness{ 0, 0, 0, 24 });
    panel.Children().Append(title);

    TextBlock version;
    char version_buffer[256];
    std::snprintf(version_buffer, sizeof(version_buffer), "Version %s (Built: %s %s)", TE_VERSION_STRING, __DATE__, __TIME__);
    version.Text(to_hstring(std::string(version_buffer)));
    version.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    panel.Children().Append(version);
    
    TextBlock desc;
    desc.Text(L"TaskbarEngine is a high-performance C17/C++17 engine for enhancing the Windows 11 Taskbar. It utilizes DirectComposition for 0-latency hardware accelerated rendering and executes directly within the Explorer process space for maximum efficiency and stability.");
    desc.TextWrapping(TextWrapping::Wrap);
    panel.Children().Append(desc);

    TextBlock docInfo;
    docInfo.Text(L"Documentation and architecture references are available in the Docs/ folder of your installation directory.");
    docInfo.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
    docInfo.TextWrapping(TextWrapping::Wrap);
    panel.Children().Append(docInfo);

    TextBlock perfTitle;
    perfTitle.Text(L"Live Performance Stats");
    perfTitle.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
    perfTitle.Margin(Thickness{ 0, 20, 0, 8 });
    panel.Children().Append(perfTitle);
    
    // Attempt to fetch perf stats via IPC
    auto statsOpt = GuiIpcGetPerfStats();
    if (statsOpt.has_value()) {
        cJSON* root = cJSON_Parse(statsOpt.value().c_str());
        if (root) {
            double fps = 0, avg_ms = 0, min_ms = 0, max_ms = 0;
            if (cJSON* fps_node = cJSON_GetObjectItem(root, "fps")) { if (cJSON_IsNumber(fps_node)) fps = fps_node->valuedouble; }
            if (cJSON* avg_node = cJSON_GetObjectItem(root, "avg_ms")) { if (cJSON_IsNumber(avg_node)) avg_ms = avg_node->valuedouble; }
            if (cJSON* min_node = cJSON_GetObjectItem(root, "min_ms")) { if (cJSON_IsNumber(min_node)) min_ms = min_node->valuedouble; }
            if (cJSON* max_node = cJSON_GetObjectItem(root, "max_ms")) { if (cJSON_IsNumber(max_node)) max_ms = max_node->valuedouble; }
            
            TextBlock perfText;
            char perf_buffer[256];
            std::snprintf(perf_buffer, sizeof(perf_buffer),
                          "Target FPS: >= 60.0\nMeasured FPS: %.1f\nAvg Frame Time: %.2f ms\nMin Frame Time: %.2f ms\nMax Frame Time: %.2f ms",
                          fps, avg_ms, min_ms, max_ms);
            std::string perfStr(perf_buffer);
            perfText.Text(to_hstring(perfStr));
            perfText.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
            panel.Children().Append(perfText);
            
            cJSON_Delete(root);
        } else {
            TextBlock perfText;
            perfText.Text(L"Unable to parse performance stats (invalid JSON).");
            panel.Children().Append(perfText);
        }
    } else {
        TextBlock perfText;
        perfText.Text(L"Engine is running in Explorer. Performance telemetry is active during user interactions.");
        panel.Children().Append(perfText);
    }
    


    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

}
