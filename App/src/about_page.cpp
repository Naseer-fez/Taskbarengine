#include "about_page.h"
#include "gui_ipc_client.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#undef FindText
#undef FindTextW
#undef FindTextA
#include <winrt/Windows.UI.Text.h>
#include <cJSON.h>
#include <cstdio>
#include <string>
#include <thread>

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
    panel.Spacing(12);
    panel.Padding(Thickness{ 32, 28, 32, 36 });
    panel.MaxWidth(920);

    StackPanel heading;
    heading.Spacing(7);
    TextBlock eyebrow;
    eyebrow.Text(L"TASKBARENGINE");
    eyebrow.FontSize(12.0);
    eyebrow.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
    eyebrow.Opacity(0.72);
    heading.Children().Append(eyebrow);
    TextBlock title;
    title.Text(L"About");
    title.FontSize(28.0);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    heading.Children().Append(title);
    panel.Children().Append(heading);

    TextBlock version;
    char version_buffer[256];
    std::snprintf(version_buffer, sizeof(version_buffer), "Version %s (Built: %s %s)", TE_VERSION_STRING, __DATE__, __TIME__);
    version.Text(to_hstring(std::string(version_buffer)));
    version.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    version.FontSize(13.0);
    version.Opacity(0.78);
    
    TextBlock desc;
    desc.Text(L"Make the Windows taskbar work the way you want. TaskbarEngine adds taskbar sizing, motion, visual effects, media controls, and widgets, managed from one place.");
    desc.TextWrapping(TextWrapping::Wrap);
    desc.FontSize(15.0);
    desc.MaxWidth(760);

    Border overview;
    overview.CornerRadius(CornerRadius{ 14, 14, 14, 14 });
    overview.BorderThickness(Thickness{ 1, 1, 1, 1 });
    overview.Padding(Thickness{ 22, 20, 22, 20 });
    StackPanel overviewContent;
    overviewContent.Spacing(10);
    overviewContent.Children().Append(version);
    overviewContent.Children().Append(desc);
    overview.Child(overviewContent);
    try {
        auto resources = Application::Current().Resources();
        if (resources.HasKey(box_value(L"CardBackgroundFillColorDefaultBrush")))
            overview.Background(resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<winrt::Microsoft::UI::Xaml::Media::Brush>());
        if (resources.HasKey(box_value(L"CardStrokeColorDefaultBrush")))
            overview.BorderBrush(resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<winrt::Microsoft::UI::Xaml::Media::Brush>());
    } catch (...) {}
    panel.Children().Append(overview);

    TextBlock docInfo;
    docInfo.Text(L"Documentation and architecture references are available in the Docs folder of your installation.");
    docInfo.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
    docInfo.TextWrapping(TextWrapping::Wrap);
    docInfo.Opacity(0.74);
    docInfo.TextWrapping(TextWrapping::Wrap);
    panel.Children().Append(docInfo);

    TextBlock perfTitle;
    perfTitle.Text(L"Engine performance");
    perfTitle.FontSize(18.0);
    perfTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    perfTitle.Margin(Thickness{ 0, 14, 0, 2 });
    panel.Children().Append(perfTitle);
    
    TextBlock perfText;
    perfText.Text(GuiIpcIsConnected()
        ? L"Connecting to the engine for performance data…"
        : L"Engine is offline. Start it to view live performance data.");
    perfText.TextWrapping(TextWrapping::Wrap);
    perfText.FontSize(14.0);
    Border performanceCard;
    performanceCard.CornerRadius(CornerRadius{ 12, 12, 12, 12 });
    performanceCard.BorderThickness(Thickness{ 1, 1, 1, 1 });
    performanceCard.Padding(Thickness{ 20, 18, 20, 18 });
    performanceCard.Child(perfText);
    try {
        auto resources = Application::Current().Resources();
        if (resources.HasKey(box_value(L"CardBackgroundFillColorDefaultBrush")))
            performanceCard.Background(resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<winrt::Microsoft::UI::Xaml::Media::Brush>());
        if (resources.HasKey(box_value(L"CardStrokeColorDefaultBrush")))
            performanceCard.BorderBrush(resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<winrt::Microsoft::UI::Xaml::Media::Brush>());
    } catch (...) {}
    panel.Children().Append(performanceCard);

    auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    std::thread([dispatcher, perfText] {
        std::string display = "Engine is not responding or performance telemetry is unavailable.";
        auto statsOpt = GuiIpcGetPerfStats();
        if (statsOpt) {
            cJSON* root = cJSON_Parse(statsOpt->c_str());
            if (root) {
                double fps = 0, avg_ms = 0, min_ms = 0, max_ms = 0;
                if (cJSON* node = cJSON_GetObjectItem(root, "fps"); cJSON_IsNumber(node)) fps = node->valuedouble;
                if (cJSON* node = cJSON_GetObjectItem(root, "avg_ms"); cJSON_IsNumber(node)) avg_ms = node->valuedouble;
                if (cJSON* node = cJSON_GetObjectItem(root, "min_ms"); cJSON_IsNumber(node)) min_ms = node->valuedouble;
                if (cJSON* node = cJSON_GetObjectItem(root, "max_ms"); cJSON_IsNumber(node)) max_ms = node->valuedouble;
                char buffer[256];
                std::snprintf(buffer, sizeof(buffer),
                    "Target FPS: >= 60.0\nMeasured FPS: %.1f\nAvg Frame Time: %.2f ms\nMin Frame Time: %.2f ms\nMax Frame Time: %.2f ms",
                    fps, avg_ms, min_ms, max_ms);
                display = buffer;
                cJSON_Delete(root);
            } else {
                display = "Unable to parse performance stats (invalid JSON).";
            }
        }
        if (dispatcher) dispatcher.TryEnqueue([perfText, display = std::move(display)] {
            try { perfText.Text(to_hstring(display)); } catch (...) {}
        });
    }).detach();
    


    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

}
