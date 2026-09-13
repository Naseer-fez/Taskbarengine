#include "settings_page.h"
#include "config_io.h"
#include "gui_ipc_client.h"
#include <cJSON.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#undef FindText
#undef FindTextW
#undef FindTextA
#include <winrt/Windows.UI.Text.h>
#include <cmath>
#include <vector>

#include <memory>
#include <functional>
#include <chrono>
#include <mutex>
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::TaskbarEngine {

static std::mutex g_settings_mutex;

static void SaveAndReload(const std::string& plugin_name, const std::string& key, cJSON* value)
{
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    std::wstring path = ConfigIO_GetConfigPath();
    cJSON* root = ConfigIO_Load(path);
    if (!root) {
        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            // File exists on disk, but loading/parsing failed (e.g. sharing violation, lock, or syntax error).
            // Do NOT overwrite existing configuration with an empty object to avoid data loss.
            if (value) cJSON_Delete(value);
            return;
        }
        root = cJSON_CreateObject();
    }
    
    if (SUCCEEDED(ConfigIO_SetPluginValue(root, plugin_name.c_str(), key.c_str(), value))) {
        if (SUCCEEDED(ConfigIO_Save(path, root))) {
            GuiIpcReloadConfig();
        }
    }
    cJSON_Delete(root);
}

static cJSON* GetCurrentValue(const std::string& plugin_name, const std::string& key)
{
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    std::wstring path = ConfigIO_GetConfigPath();
    cJSON* root = ConfigIO_Load(path);
    cJSON* res = nullptr;
    if (root) {
        cJSON* val = ConfigIO_GetPluginValue(root, plugin_name.c_str(), key.c_str());
        if (val) {
            res = cJSON_Duplicate(val, 1);
        }
        cJSON_Delete(root);
    }
    return res;
}

struct SettingKeyValue {
    std::string key;
    cJSON* value; // takes ownership
};

static void SaveMultipleAndReload(const std::string& plugin_name, std::vector<SettingKeyValue> items)
{
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    std::wstring path = ConfigIO_GetConfigPath();
    cJSON* root = ConfigIO_Load(path);
    if (!root) {
        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            for (auto& it : items) {
                if (it.value) cJSON_Delete(it.value);
            }
            return;
        }
        root = cJSON_CreateObject();
    }

    for (auto& it : items) {
        ConfigIO_SetPluginValue(root, plugin_name.c_str(), it.key.c_str(), it.value);
    }

    if (SUCCEEDED(ConfigIO_Save(path, root))) {
        GuiIpcReloadConfig();
    }
    cJSON_Delete(root);
}

static double GetDoubleSetting(const std::string& plugin_name, const std::string& key, double default_val)
{
    cJSON* node = GetCurrentValue(plugin_name, key);
    if (node) {
        if (cJSON_IsNumber(node)) {
            double v = node->valuedouble;
            cJSON_Delete(node);
            return v;
        }
        cJSON_Delete(node);
    }
    return default_val;
}

static bool GetBoolSetting(const std::string& plugin_name, const std::string& key, bool default_val)
{
    cJSON* node = GetCurrentValue(plugin_name, key);
    if (node) {
        if (cJSON_IsBool(node)) {
            bool v = cJSON_IsTrue(node);
            cJSON_Delete(node);
            return v;
        }
        cJSON_Delete(node);
    }
    return default_val;
}

static std::string GetStringSetting(const std::string& plugin_name, const std::string& key, const std::string& default_val)
{
    cJSON* node = GetCurrentValue(plugin_name, key);
    if (node) {
        if (cJSON_IsString(node) && node->valuestring) {
            std::string v = node->valuestring;
            cJSON_Delete(node);
            return v;
        }
        cJSON_Delete(node);
    }
    return default_val;
}

static bool PickImageFile(std::wstring& out_path)
{
    wchar_t szFile[MAX_PATH] = { 0 };

    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Image Files (*.png;*.svg;*.jpg;*.jpeg;*.bmp;*.ico)\0*.png;*.svg;*.jpg;*.jpeg;*.bmp;*.ico\0PNG Images (*.png)\0*.png\0SVG Images (*.svg)\0*.svg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    std::wstring cfg_path = ConfigIO_GetConfigPath();
    std::wstring init_dir;
    size_t last_slash = cfg_path.find_last_of(L"\\/");
    if (last_slash != std::wstring::npos) {
        init_dir = cfg_path.substr(0, last_slash);
        ofn.lpstrInitialDir = init_dir.c_str();
    }

    if (GetOpenFileNameW(&ofn)) {
        out_path = szFile;
        return true;
    }
    return false;
}

static Page CreateIconHoverSettingsPage()
{
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(16);
    panel.Padding(Thickness{ 28, 24, 28, 28 });

    // Title
    TextBlock title;
    title.Text(L"Icon Magnification & Physics");
    title.FontSize(24.0);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    title.Margin(Thickness{ 0, 0, 0, 4 });
    panel.Children().Append(title);

    // Subtitle
    TextBlock subtitle;
    subtitle.Text(L"Configure macOS Dock-style magnification scale, physics influence radius, duration, and falloff curves.");
    subtitle.FontSize(13.0);
    subtitle.Opacity(0.7);
    subtitle.Margin(Thickness{ 0, 0, 0, 20 });
    panel.Children().Append(subtitle);

    const std::string plugin_name = "icon_hover";

    // 1. Enable Toggle
    {
        StackPanel card;
        card.Spacing(4);
        card.Margin(Thickness{ 0, 0, 0, 16 });

        ToggleSwitch toggle;
        toggle.Header(box_value(L"Enable Icon Magnification"));
        toggle.OffContent(box_value(L"Disabled"));
        toggle.OnContent(box_value(L"Enabled"));
        bool enabled = GetBoolSetting(plugin_name, "enabled", true);
        toggle.IsOn(enabled);
        toggle.Toggled([plugin_name](IInspectable const& sender, RoutedEventArgs const&) {
            SaveAndReload(plugin_name, "enabled", cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
        });
        card.Children().Append(toggle);
        panel.Children().Append(card);
    }

    auto max_scale_slider = std::make_shared<Slider>();
    auto radius_slider = std::make_shared<Slider>();
    auto speed_slider = std::make_shared<Slider>();
    auto curve_combo = std::make_shared<ComboBox>();
    auto bounce_toggle = std::make_shared<ToggleSwitch>();
    auto bounce_strength_slider = std::make_shared<Slider>();
    auto keep_on_top_toggle = std::make_shared<ToggleSwitch>();
    auto tilt_toggle = std::make_shared<ToggleSwitch>();
    auto tilt_angle_slider = std::make_shared<Slider>();
    auto drag_drop_toggle = std::make_shared<ToggleSwitch>();
    auto drag_recession_slider = std::make_shared<Slider>();
    auto drop_zone_push_slider = std::make_shared<Slider>();
    auto start_image_box = std::make_shared<TextBox>();

    // 2. max_scale Slider (1.0 to 10.0)
    {
        StackPanel card;
        card.Spacing(4);
        card.Margin(Thickness{ 0, 0, 0, 16 });

        StackPanel header;
        header.Orientation(Orientation::Horizontal);
        header.Spacing(12);

        TextBlock lbl;
        lbl.Text(L"Max Magnification Scale (max_scale)");
        lbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        lbl.FontSize(14.0);
        header.Children().Append(lbl);

        double cur_val = GetDoubleSetting(plugin_name, "max_scale", 1.2);
        if (cur_val < 1.0) cur_val = 1.0;
        if (cur_val > 10.0) cur_val = 10.0;

        char buf[32];
        snprintf(buf, sizeof(buf), "%.2fx", cur_val);
        TextBlock valLbl;
        valLbl.Text(to_hstring(buf));
        valLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        valLbl.FontSize(14.0);
        header.Children().Append(valLbl);
        card.Children().Append(header);

        TextBlock desc;
        desc.Text(L"Peak scale multiplier when cursor is directly over an icon (1.0x to 10.0x).");
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        card.Children().Append(desc);

        max_scale_slider->Minimum(1.0);
        max_scale_slider->Maximum(10.0);
        max_scale_slider->StepFrequency(0.05);
        max_scale_slider->SmallChange(0.05);
        max_scale_slider->LargeChange(0.5);
        max_scale_slider->Value(cur_val);
        max_scale_slider->IsThumbToolTipEnabled(true);
        max_scale_slider->Width(460);
        max_scale_slider->HorizontalAlignment(HorizontalAlignment::Left);
        max_scale_slider->Margin(Thickness{ 0, 4, 0, 0 });

        NumberBox max_scale_box;
        max_scale_box.Width(95);
        max_scale_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
        max_scale_box.Minimum(1.0);
        max_scale_box.Maximum(10.0);
        max_scale_box.SmallChange(0.05);
        max_scale_box.LargeChange(0.5);
        max_scale_box.Value(cur_val);
        max_scale_box.VerticalAlignment(VerticalAlignment::Center);

        auto is_updating_scale = std::make_shared<bool>(false);

        auto debounce = std::make_shared<DispatcherTimer>();
        debounce->Interval(std::chrono::milliseconds(100));
        auto s = *max_scale_slider;
        debounce->Tick([plugin_name, s, debounce](IInspectable const&, IInspectable const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "max_scale", cJSON_CreateNumber(s.Value()));
        });

        max_scale_slider->ValueChanged([valLbl, max_scale_box, is_updating_scale, debounce](auto const&, auto const& e) {
            if (!*is_updating_scale) {
                *is_updating_scale = true;
                max_scale_box.Value(e.NewValue());
                *is_updating_scale = false;
            }
            char b[32];
            snprintf(b, sizeof(b), "%.2fx", e.NewValue());
            valLbl.Text(to_hstring(b));
            debounce->Stop();
            debounce->Start();
        });

        max_scale_box.ValueChanged([valLbl, max_scale_slider, is_updating_scale, debounce](NumberBox const&, NumberBoxValueChangedEventArgs const& args) {
            if (!*is_updating_scale && !std::isnan(args.NewValue())) {
                *is_updating_scale = true;
                max_scale_slider->Value(args.NewValue());
                *is_updating_scale = false;
                char b[32];
                snprintf(b, sizeof(b), "%.2fx", args.NewValue());
                valLbl.Text(to_hstring(b));
                debounce->Stop();
                debounce->Start();
            }
        });

        max_scale_slider->LostFocus([plugin_name, s, debounce](IInspectable const&, RoutedEventArgs const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "max_scale", cJSON_CreateNumber(s.Value()));
        });

        StackPanel controlRow;
        controlRow.Orientation(Orientation::Horizontal);
        controlRow.Spacing(16);
        controlRow.Children().Append(*max_scale_slider);
        controlRow.Children().Append(max_scale_box);
        card.Children().Append(controlRow);
        panel.Children().Append(card);
    }

    // 3. radius Slider (50 to 500)
    {
        StackPanel card;
        card.Spacing(4);
        card.Margin(Thickness{ 0, 0, 0, 16 });

        StackPanel header;
        header.Orientation(Orientation::Horizontal);
        header.Spacing(12);

        TextBlock lbl;
        lbl.Text(L"Effect Radius (radius)");
        lbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        lbl.FontSize(14.0);
        header.Children().Append(lbl);

        double cur_val = GetDoubleSetting(plugin_name, "radius", 150.0);
        if (cur_val < 50.0) cur_val = 50.0;
        if (cur_val > 500.0) cur_val = 500.0;

        char buf[32];
        snprintf(buf, sizeof(buf), "%d px", (int)std::round(cur_val));
        TextBlock valLbl;
        valLbl.Text(to_hstring(buf));
        valLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        valLbl.FontSize(14.0);
        header.Children().Append(valLbl);
        card.Children().Append(header);

        TextBlock desc;
        desc.Text(L"Distance in pixels over which neighboring icons are influenced (50 to 500 px).");
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        card.Children().Append(desc);

        radius_slider->Minimum(50.0);
        radius_slider->Maximum(500.0);
        radius_slider->StepFrequency(1.0);
        radius_slider->SmallChange(1.0);
        radius_slider->LargeChange(25.0);
        radius_slider->Value(cur_val);
        radius_slider->IsThumbToolTipEnabled(true);
        radius_slider->Width(460);
        radius_slider->HorizontalAlignment(HorizontalAlignment::Left);
        radius_slider->Margin(Thickness{ 0, 4, 0, 0 });

        NumberBox radius_box;
        radius_box.Width(95);
        radius_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
        radius_box.Minimum(50.0);
        radius_box.Maximum(500.0);
        radius_box.SmallChange(5.0);
        radius_box.LargeChange(25.0);
        radius_box.Value(cur_val);
        radius_box.VerticalAlignment(VerticalAlignment::Center);

        auto is_updating_radius = std::make_shared<bool>(false);

        auto debounce = std::make_shared<DispatcherTimer>();
        debounce->Interval(std::chrono::milliseconds(100));
        auto s = *radius_slider;
        debounce->Tick([plugin_name, s, debounce](IInspectable const&, IInspectable const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "radius", cJSON_CreateNumber(std::round(s.Value())));
        });

        radius_slider->ValueChanged([valLbl, radius_box, is_updating_radius, debounce](auto const&, auto const& e) {
            if (!*is_updating_radius) {
                *is_updating_radius = true;
                radius_box.Value(std::round(e.NewValue()));
                *is_updating_radius = false;
            }
            char b[32];
            snprintf(b, sizeof(b), "%d px", (int)std::round(e.NewValue()));
            valLbl.Text(to_hstring(b));
            debounce->Stop();
            debounce->Start();
        });

        radius_box.ValueChanged([valLbl, radius_slider, is_updating_radius, debounce](NumberBox const&, NumberBoxValueChangedEventArgs const& args) {
            if (!*is_updating_radius && !std::isnan(args.NewValue())) {
                *is_updating_radius = true;
                radius_slider->Value(std::round(args.NewValue()));
                *is_updating_radius = false;
                char b[32];
                snprintf(b, sizeof(b), "%d px", (int)std::round(args.NewValue()));
                valLbl.Text(to_hstring(b));
                debounce->Stop();
                debounce->Start();
            }
        });

        radius_slider->LostFocus([plugin_name, s, debounce](IInspectable const&, RoutedEventArgs const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "radius", cJSON_CreateNumber(std::round(s.Value())));
        });

        StackPanel controlRow;
        controlRow.Orientation(Orientation::Horizontal);
        controlRow.Spacing(16);
        controlRow.Children().Append(*radius_slider);
        controlRow.Children().Append(radius_box);
        card.Children().Append(controlRow);
        panel.Children().Append(card);
    }

    // 4. speed_ms Slider (50 to 500)
    {
        StackPanel card;
        card.Spacing(4);
        card.Margin(Thickness{ 0, 0, 0, 16 });

        StackPanel header;
        header.Orientation(Orientation::Horizontal);
        header.Spacing(12);

        TextBlock lbl;
        lbl.Text(L"Animation Duration (speed_ms)");
        lbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        lbl.FontSize(14.0);
        header.Children().Append(lbl);

        double cur_val = GetDoubleSetting(plugin_name, "speed_ms", 150.0);
        if (cur_val < 50.0) cur_val = 50.0;
        if (cur_val > 500.0) cur_val = 500.0;

        char buf[32];
        snprintf(buf, sizeof(buf), "%d ms", (int)std::round(cur_val));
        TextBlock valLbl;
        valLbl.Text(to_hstring(buf));
        valLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        valLbl.FontSize(14.0);
        header.Children().Append(valLbl);
        card.Children().Append(header);

        TextBlock desc;
        desc.Text(L"Duration in milliseconds for the settle animation when mouse leaves (50 to 500 ms).");
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        card.Children().Append(desc);

        speed_slider->Minimum(50.0);
        speed_slider->Maximum(500.0);
        speed_slider->StepFrequency(5.0);
        speed_slider->SmallChange(5.0);
        speed_slider->LargeChange(25.0);
        speed_slider->Value(cur_val);
        speed_slider->IsThumbToolTipEnabled(true);
        speed_slider->Width(460);
        speed_slider->HorizontalAlignment(HorizontalAlignment::Left);
        speed_slider->Margin(Thickness{ 0, 4, 0, 0 });

        NumberBox speed_box;
        speed_box.Width(95);
        speed_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
        speed_box.Minimum(50.0);
        speed_box.Maximum(500.0);
        speed_box.SmallChange(10.0);
        speed_box.LargeChange(50.0);
        speed_box.Value(cur_val);
        speed_box.VerticalAlignment(VerticalAlignment::Center);

        auto is_updating_speed = std::make_shared<bool>(false);

        auto debounce = std::make_shared<DispatcherTimer>();
        debounce->Interval(std::chrono::milliseconds(100));
        auto s = *speed_slider;
        debounce->Tick([plugin_name, s, debounce](IInspectable const&, IInspectable const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "speed_ms", cJSON_CreateNumber(std::round(s.Value())));
        });

        speed_slider->ValueChanged([valLbl, speed_box, is_updating_speed, debounce](auto const&, auto const& e) {
            if (!*is_updating_speed) {
                *is_updating_speed = true;
                speed_box.Value(std::round(e.NewValue()));
                *is_updating_speed = false;
            }
            char b[32];
            snprintf(b, sizeof(b), "%d ms", (int)std::round(e.NewValue()));
            valLbl.Text(to_hstring(b));
            debounce->Stop();
            debounce->Start();
        });

        speed_box.ValueChanged([valLbl, speed_slider, is_updating_speed, debounce](NumberBox const&, NumberBoxValueChangedEventArgs const& args) {
            if (!*is_updating_speed && !std::isnan(args.NewValue())) {
                *is_updating_speed = true;
                speed_slider->Value(std::round(args.NewValue()));
                *is_updating_speed = false;
                char b[32];
                snprintf(b, sizeof(b), "%d ms", (int)std::round(args.NewValue()));
                valLbl.Text(to_hstring(b));
                debounce->Stop();
                debounce->Start();
            }
        });

        speed_slider->LostFocus([plugin_name, s, debounce](IInspectable const&, RoutedEventArgs const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "speed_ms", cJSON_CreateNumber(std::round(s.Value())));
        });

        StackPanel controlRow;
        controlRow.Orientation(Orientation::Horizontal);
        controlRow.Spacing(16);
        controlRow.Children().Append(*speed_slider);
        controlRow.Children().Append(speed_box);
        card.Children().Append(controlRow);
        panel.Children().Append(card);
    }

    // 5. curve Dropdown ("gaussian", "cubic", "cosine", "linear")
    {
        StackPanel card;
        card.Spacing(4);
        card.Margin(Thickness{ 0, 0, 0, 20 });

        TextBlock lbl;
        lbl.Text(L"Easing Falloff Curve (curve)");
        lbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        lbl.FontSize(14.0);
        card.Children().Append(lbl);

        TextBlock desc;
        desc.Text(L"Mathematical curve controlling magnification falloff profile across neighboring icons.");
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        card.Children().Append(desc);

        const std::vector<std::string> curve_opts = { "gaussian", "cubic", "cosine", "linear" };
        std::string cur_curve = GetStringSetting(plugin_name, "curve", "gaussian");

        int sel_idx = 0;
        for (size_t i = 0; i < curve_opts.size(); ++i) {
            curve_combo->Items().Append(box_value(to_hstring(curve_opts[i])));
            if (_stricmp(cur_curve.c_str(), curve_opts[i].c_str()) == 0) {
                sel_idx = static_cast<int>(i);
            }
        }
        curve_combo->SelectedIndex(sel_idx);
        curve_combo->MinWidth(220);
        curve_combo->HorizontalAlignment(HorizontalAlignment::Left);
        curve_combo->Margin(Thickness{ 0, 4, 0, 0 });

        curve_combo->SelectionChanged([plugin_name, curve_opts](IInspectable const& sender, SelectionChangedEventArgs const&) {
            int idx = sender.as<ComboBox>().SelectedIndex();
            if (idx >= 0 && static_cast<size_t>(idx) < curve_opts.size()) {
                SaveAndReload(plugin_name, "curve", cJSON_CreateString(curve_opts[idx].c_str()));
            }
        });

        card.Children().Append(*curve_combo);
        panel.Children().Append(card);
    }

    // 6. Notification Inertial Bouncing Section
    {
        StackPanel card;
        card.Spacing(6);
        card.Margin(Thickness{ 0, 0, 0, 20 });

        TextBlock secTitle;
        secTitle.Text(L"Notification Inertial Bouncing");
        secTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        secTitle.FontSize(15.0);
        card.Children().Append(secTitle);

        TextBlock desc;
        desc.Text(L"Taskbar icons perform an upward elastic spring bounce when receiving background notifications or alerts.");
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        card.Children().Append(desc);

        // Toggle Switch
        bounce_toggle->Header(box_value(L"Enable Notification Bounce (bounce_enabled)"));
        bounce_toggle->OffContent(box_value(L"Disabled"));
        bounce_toggle->OnContent(box_value(L"Enabled"));
        bool bounce_on = GetBoolSetting(plugin_name, "bounce_enabled", true);
        bounce_toggle->IsOn(bounce_on);
        bounce_toggle->Margin(Thickness{ 0, 4, 0, 8 });
        bounce_toggle->Toggled([plugin_name](IInspectable const& sender, RoutedEventArgs const&) {
            SaveAndReload(plugin_name, "bounce_enabled", cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
        });
        card.Children().Append(*bounce_toggle);

        // Strength Header & Label
        StackPanel strengthHeader;
        strengthHeader.Orientation(Orientation::Horizontal);
        strengthHeader.Spacing(12);

        TextBlock strengthLbl;
        strengthLbl.Text(L"Bounce Impulse Strength (bounce_strength)");
        strengthLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        strengthLbl.FontSize(14.0);
        strengthHeader.Children().Append(strengthLbl);

        double cur_strength = GetDoubleSetting(plugin_name, "bounce_strength", 600.0);
        if (cur_strength < 200.0) cur_strength = 200.0;
        if (cur_strength > 1500.0) cur_strength = 1500.0;

        char buf[32];
        snprintf(buf, sizeof(buf), "%d px/s", (int)std::round(cur_strength));
        TextBlock valLbl;
        valLbl.Text(to_hstring(buf));
        valLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        valLbl.FontSize(14.0);
        strengthHeader.Children().Append(valLbl);
        card.Children().Append(strengthHeader);

        TextBlock strengthDesc;
        strengthDesc.Text(L"Initial upward launch velocity applied when an app receives a notification (200 to 1500 px/s).");
        strengthDesc.FontSize(12.0);
        strengthDesc.Opacity(0.7);
        card.Children().Append(strengthDesc);

        bounce_strength_slider->Minimum(200.0);
        bounce_strength_slider->Maximum(1500.0);
        bounce_strength_slider->StepFrequency(25.0);
        bounce_strength_slider->SmallChange(25.0);
        bounce_strength_slider->LargeChange(100.0);
        bounce_strength_slider->Value(cur_strength);
        bounce_strength_slider->IsThumbToolTipEnabled(true);
        bounce_strength_slider->Width(460);
        bounce_strength_slider->HorizontalAlignment(HorizontalAlignment::Left);
        bounce_strength_slider->Margin(Thickness{ 0, 4, 0, 0 });

        NumberBox bounce_strength_box;
        bounce_strength_box.Width(95);
        bounce_strength_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
        bounce_strength_box.Minimum(200.0);
        bounce_strength_box.Maximum(1500.0);
        bounce_strength_box.SmallChange(25.0);
        bounce_strength_box.LargeChange(100.0);
        bounce_strength_box.Value(cur_strength);
        bounce_strength_box.VerticalAlignment(VerticalAlignment::Center);

        auto is_updating_strength = std::make_shared<bool>(false);

        auto debounce = std::make_shared<DispatcherTimer>();
        debounce->Interval(std::chrono::milliseconds(100));
        auto s = *bounce_strength_slider;
        debounce->Tick([plugin_name, s, debounce](IInspectable const&, IInspectable const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "bounce_strength", cJSON_CreateNumber(std::round(s.Value())));
        });

        bounce_strength_slider->ValueChanged([valLbl, bounce_strength_box, is_updating_strength, debounce](auto const&, auto const& e) {
            if (!*is_updating_strength) {
                *is_updating_strength = true;
                bounce_strength_box.Value(std::round(e.NewValue()));
                *is_updating_strength = false;
                char b[32];
                snprintf(b, sizeof(b), "%d px/s", (int)std::round(e.NewValue()));
                valLbl.Text(to_hstring(b));
                debounce->Stop();
                debounce->Start();
            }
        });

        bounce_strength_box.ValueChanged([valLbl, bounce_strength_slider, is_updating_strength, debounce](auto const&, auto const& args) {
            if (!*is_updating_strength) {
                *is_updating_strength = true;
                bounce_strength_slider->Value(std::round(args.NewValue()));
                *is_updating_strength = false;
                char b[32];
                snprintf(b, sizeof(b), "%d px/s", (int)std::round(args.NewValue()));
                valLbl.Text(to_hstring(b));
                debounce->Stop();
                debounce->Start();
            }
        });

        bounce_strength_slider->LostFocus([plugin_name, s, debounce](IInspectable const&, RoutedEventArgs const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "bounce_strength", cJSON_CreateNumber(std::round(s.Value())));
        });

        StackPanel controlRow;
        controlRow.Orientation(Orientation::Horizontal);
        controlRow.Spacing(16);
        controlRow.Children().Append(*bounce_strength_slider);
        controlRow.Children().Append(bounce_strength_box);
        card.Children().Append(controlRow);

        // Test Notification Bounce Button Row
        StackPanel testRow;
        testRow.Orientation(Orientation::Horizontal);
        testRow.Spacing(12);
        testRow.Margin(Thickness{ 0, 10, 0, 0 });

        Button testBtn;
        testBtn.Content(box_value(L"Test Notification Bounce"));
        testBtn.Padding(Thickness{ 16, 6, 16, 6 });

        TextBlock testStatus;
        testStatus.VerticalAlignment(VerticalAlignment::Center);
        testStatus.FontSize(12.0);
        testStatus.Opacity(0.8);

        testBtn.Click([testStatus](IInspectable const&, RoutedEventArgs const&) {
            testStatus.Text(L"Switch window or click away in 2s to see bounce...");
            auto timer = std::make_shared<DispatcherTimer>();
            timer->Interval(std::chrono::milliseconds(2000));
            timer->Tick([timer, testStatus](IInspectable const&, IInspectable const&) {
                timer->Stop();
                testStatus.Text(L"Triggered test flash!");
                HWND topWnd = GetActiveWindow();
                if (!topWnd) topWnd = GetForegroundWindow();
                if (topWnd) {
                    FLASHWINFO fwi = {};
                    fwi.cbSize = sizeof(fwi);
                    fwi.hwnd = topWnd;
                    fwi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
                    fwi.uCount = 3;
                    fwi.dwTimeout = 0;
                    FlashWindowEx(&fwi);
                }
            });
            timer->Start();
        });

        testRow.Children().Append(testBtn);
        testRow.Children().Append(testStatus);
        card.Children().Append(testRow);

        panel.Children().Append(card);
    }

    // 7. Keep On Top (Z-Order Protection)
    {
        StackPanel card;
        card.Spacing(4);
        card.Margin(Thickness{ 0, 0, 0, 16 });

        keep_on_top_toggle->Header(box_value(L"Keep Overlay On Top (Z-Order Protection)"));
        keep_on_top_toggle->OffContent(box_value(L"Disabled"));
        keep_on_top_toggle->OnContent(box_value(L"Enabled"));
        bool keep_top = GetBoolSetting(plugin_name, "keep_on_top", true);
        keep_on_top_toggle->IsOn(keep_top);
        keep_on_top_toggle->Margin(Thickness{ 0, 4, 0, 4 });
        keep_on_top_toggle->Toggled([plugin_name](IInspectable const& sender, RoutedEventArgs const&) {
            SaveAndReload(plugin_name, "keep_on_top", cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
        });
        card.Children().Append(*keep_on_top_toggle);

        TextBlock desc;
        desc.Text(L"Continuously enforces HWND_TOPMOST so taskbar clicks do not obscure magnification animations.");
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        card.Children().Append(desc);

        panel.Children().Append(card);
    }

    // 8. 3D Tilt & Perspective Section
    {
        StackPanel card;
        card.Spacing(6);
        card.Margin(Thickness{ 0, 0, 0, 20 });

        TextBlock secTitle;
        secTitle.Text(L"3D Tilt & Perspective");
        secTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        secTitle.FontSize(15.0);
        card.Children().Append(secTitle);

        TextBlock desc;
        desc.Text(L"DirectComposition 3D matrix transformation tilting icons toward the cursor in real time with spring physics.");
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        card.Children().Append(desc);

        // Tilt Toggle Switch
        tilt_toggle->Header(box_value(L"Enable 3D Tilt Perspective (tilt_enabled)"));
        tilt_toggle->OffContent(box_value(L"Disabled"));
        tilt_toggle->OnContent(box_value(L"Enabled"));
        bool tilt_on = GetBoolSetting(plugin_name, "tilt_enabled", true);
        tilt_toggle->IsOn(tilt_on);
        tilt_toggle->Margin(Thickness{ 0, 4, 0, 8 });
        tilt_toggle->Toggled([plugin_name](IInspectable const& sender, RoutedEventArgs const&) {
            SaveAndReload(plugin_name, "tilt_enabled", cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
        });
        card.Children().Append(*tilt_toggle);

        // Max Tilt Angle Header & Label
        StackPanel angleHeader;
        angleHeader.Orientation(Orientation::Horizontal);
        angleHeader.Spacing(12);

        TextBlock angleLbl;
        angleLbl.Text(L"Maximum Tilt Angle (max_tilt_angle)");
        angleLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        angleLbl.FontSize(14.0);
        angleHeader.Children().Append(angleLbl);

        double cur_angle = GetDoubleSetting(plugin_name, "max_tilt_angle", 20.0);
        if (cur_angle < 0.0) cur_angle = 0.0;
        if (cur_angle > 45.0) cur_angle = 45.0;

        char buf[32];
        snprintf(buf, sizeof(buf), "%d\u00b0", (int)std::round(cur_angle));
        TextBlock valLbl;
        valLbl.Text(to_hstring(buf));
        valLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        valLbl.FontSize(14.0);
        angleHeader.Children().Append(valLbl);
        card.Children().Append(angleHeader);

        TextBlock angleDesc;
        angleDesc.Text(L"Maximum pitch and yaw rotation angle in degrees when cursor is near icon boundaries (0\u00b0 to 45\u00b0).");
        angleDesc.FontSize(12.0);
        angleDesc.Opacity(0.7);
        card.Children().Append(angleDesc);

        tilt_angle_slider->Minimum(0.0);
        tilt_angle_slider->Maximum(45.0);
        tilt_angle_slider->StepFrequency(1.0);
        tilt_angle_slider->SmallChange(1.0);
        tilt_angle_slider->LargeChange(5.0);
        tilt_angle_slider->Value(cur_angle);
        tilt_angle_slider->IsThumbToolTipEnabled(true);
        tilt_angle_slider->Width(460);
        tilt_angle_slider->HorizontalAlignment(HorizontalAlignment::Left);
        tilt_angle_slider->Margin(Thickness{ 0, 4, 0, 0 });

        NumberBox tilt_angle_box;
        tilt_angle_box.Width(95);
        tilt_angle_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
        tilt_angle_box.Minimum(0.0);
        tilt_angle_box.Maximum(45.0);
        tilt_angle_box.SmallChange(1.0);
        tilt_angle_box.LargeChange(5.0);
        tilt_angle_box.Value(cur_angle);
        tilt_angle_box.VerticalAlignment(VerticalAlignment::Center);

        auto is_updating_tilt = std::make_shared<bool>(false);

        auto debounce = std::make_shared<DispatcherTimer>();
        debounce->Interval(std::chrono::milliseconds(100));
        auto s = *tilt_angle_slider;
        debounce->Tick([plugin_name, s, debounce](IInspectable const&, IInspectable const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "max_tilt_angle", cJSON_CreateNumber(std::round(s.Value())));
        });

        tilt_angle_slider->ValueChanged([valLbl, tilt_angle_box, is_updating_tilt, debounce](auto const&, auto const& e) {
            if (!*is_updating_tilt) {
                *is_updating_tilt = true;
                tilt_angle_box.Value(std::round(e.NewValue()));
                *is_updating_tilt = false;
            }
            char b[32];
            snprintf(b, sizeof(b), "%d\u00b0", (int)std::round(e.NewValue()));
            valLbl.Text(to_hstring(b));
            debounce->Stop();
            debounce->Start();
        });

        tilt_angle_box.ValueChanged([valLbl, tilt_angle_slider, is_updating_tilt, debounce](auto const&, auto const& args) {
            if (!*is_updating_tilt && !std::isnan(args.NewValue())) {
                *is_updating_tilt = true;
                tilt_angle_slider->Value(std::round(args.NewValue()));
                *is_updating_tilt = false;
                char b[32];
                snprintf(b, sizeof(b), "%d\u00b0", (int)std::round(args.NewValue()));
                valLbl.Text(to_hstring(b));
                debounce->Stop();
                debounce->Start();
            }
        });

        tilt_angle_slider->LostFocus([plugin_name, s, debounce](IInspectable const&, RoutedEventArgs const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, "max_tilt_angle", cJSON_CreateNumber(std::round(s.Value())));
        });

        StackPanel controlRow;
        controlRow.Orientation(Orientation::Horizontal);
        controlRow.Spacing(16);
        controlRow.Children().Append(*tilt_angle_slider);
        controlRow.Children().Append(tilt_angle_box);
        card.Children().Append(controlRow);

        panel.Children().Append(card);
    }

    // 9. Drag-and-Drop Physics Section
    {
        StackPanel card;
        card.Spacing(6);
        card.Margin(Thickness{ 0, 0, 0, 20 });

        TextBlock secTitle;
        secTitle.Text(L"Drag-and-Drop Physics");
        secTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        secTitle.FontSize(15.0);
        card.Children().Append(secTitle);

        TextBlock desc;
        desc.Text(L"During mouse drag operations, scales down the held icon and spreads adjacent icons apart horizontally to create an expansive drop target zone.");
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        card.Children().Append(desc);

        // Drag Toggle Switch
        drag_drop_toggle->Header(box_value(L"Enable Drag & Drop Physics (drag_drop_enabled)"));
        drag_drop_toggle->OffContent(box_value(L"Disabled"));
        drag_drop_toggle->OnContent(box_value(L"Enabled"));
        bool drag_on = GetBoolSetting(plugin_name, "drag_drop_enabled", true);
        drag_drop_toggle->IsOn(drag_on);
        drag_drop_toggle->Margin(Thickness{ 0, 4, 0, 8 });
        drag_drop_toggle->Toggled([plugin_name](IInspectable const& sender, RoutedEventArgs const&) {
            SaveAndReload(plugin_name, "drag_drop_enabled", cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
        });
        card.Children().Append(*drag_drop_toggle);

        // 1) Drag Recession Scale
        StackPanel recessionHeader;
        recessionHeader.Orientation(Orientation::Horizontal);
        recessionHeader.Spacing(12);

        TextBlock recessionLbl;
        recessionLbl.Text(L"Drag Recession Scale (drag_recession_scale)");
        recessionLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        recessionLbl.FontSize(14.0);
        recessionHeader.Children().Append(recessionLbl);

        double cur_recession = GetDoubleSetting(plugin_name, "drag_recession_scale", 0.80);
        if (cur_recession < 0.50) cur_recession = 0.50;
        if (cur_recession > 1.00) cur_recession = 1.00;

        char buf[32];
        snprintf(buf, sizeof(buf), "%.2fx", cur_recession);
        TextBlock valLblRec;
        valLblRec.Text(to_hstring(buf));
        valLblRec.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        valLblRec.FontSize(14.0);
        recessionHeader.Children().Append(valLblRec);
        card.Children().Append(recessionHeader);

        TextBlock recessionDesc;
        recessionDesc.Text(L"Target scale multiplier applied to an icon while being clicked and dragged (0.50x to 1.00x).");
        recessionDesc.FontSize(12.0);
        recessionDesc.Opacity(0.7);
        card.Children().Append(recessionDesc);

        drag_recession_slider->Minimum(0.50);
        drag_recession_slider->Maximum(1.00);
        drag_recession_slider->StepFrequency(0.05);
        drag_recession_slider->SmallChange(0.05);
        drag_recession_slider->LargeChange(0.10);
        drag_recession_slider->Value(cur_recession);
        drag_recession_slider->IsThumbToolTipEnabled(true);
        drag_recession_slider->Width(460);
        drag_recession_slider->HorizontalAlignment(HorizontalAlignment::Left);
        drag_recession_slider->Margin(Thickness{ 0, 4, 0, 0 });

        NumberBox drag_recession_box;
        drag_recession_box.Width(95);
        drag_recession_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
        drag_recession_box.Minimum(0.50);
        drag_recession_box.Maximum(1.00);
        drag_recession_box.SmallChange(0.05);
        drag_recession_box.LargeChange(0.10);
        drag_recession_box.Value(cur_recession);
        drag_recession_box.VerticalAlignment(VerticalAlignment::Center);

        auto is_updating_recession = std::make_shared<bool>(false);

        auto debounce_rec = std::make_shared<DispatcherTimer>();
        debounce_rec->Interval(std::chrono::milliseconds(100));
        auto s_rec = *drag_recession_slider;
        debounce_rec->Tick([plugin_name, s_rec, debounce_rec](IInspectable const&, IInspectable const&) {
            debounce_rec->Stop();
            SaveAndReload(plugin_name, "drag_recession_scale", cJSON_CreateNumber(s_rec.Value()));
        });

        drag_recession_slider->ValueChanged([valLblRec, drag_recession_box, is_updating_recession, debounce_rec](auto const&, auto const& e) {
            if (!*is_updating_recession) {
                *is_updating_recession = true;
                drag_recession_box.Value(e.NewValue());
                *is_updating_recession = false;
            }
            char b[32];
            snprintf(b, sizeof(b), "%.2fx", e.NewValue());
            valLblRec.Text(to_hstring(b));
            debounce_rec->Stop();
            debounce_rec->Start();
        });

        drag_recession_box.ValueChanged([valLblRec, drag_recession_slider, is_updating_recession, debounce_rec](auto const&, auto const& args) {
            if (!*is_updating_recession && !std::isnan(args.NewValue())) {
                *is_updating_recession = true;
                drag_recession_slider->Value(args.NewValue());
                *is_updating_recession = false;
                char b[32];
                snprintf(b, sizeof(b), "%.2fx", args.NewValue());
                valLblRec.Text(to_hstring(b));
                debounce_rec->Stop();
                debounce_rec->Start();
            }
        });

        drag_recession_slider->LostFocus([plugin_name, s_rec, debounce_rec](IInspectable const&, RoutedEventArgs const&) {
            debounce_rec->Stop();
            SaveAndReload(plugin_name, "drag_recession_scale", cJSON_CreateNumber(s_rec.Value()));
        });

        StackPanel recRow;
        recRow.Orientation(Orientation::Horizontal);
        recRow.Spacing(16);
        recRow.Children().Append(*drag_recession_slider);
        recRow.Children().Append(drag_recession_box);
        card.Children().Append(recRow);

        // 2) Drop Zone Push Factor
        StackPanel pushHeader;
        pushHeader.Orientation(Orientation::Horizontal);
        pushHeader.Spacing(12);
        pushHeader.Margin(Thickness{ 0, 12, 0, 0 });

        TextBlock pushLbl;
        pushLbl.Text(L"Drop Zone Push Distance (drop_zone_push)");
        pushLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        pushLbl.FontSize(14.0);
        pushHeader.Children().Append(pushLbl);

        double cur_push = GetDoubleSetting(plugin_name, "drop_zone_push", 50.0);
        if (cur_push < 10.0) cur_push = 10.0;
        if (cur_push > 120.0) cur_push = 120.0;

        char bufPush[32];
        snprintf(bufPush, sizeof(bufPush), "%d px", (int)std::round(cur_push));
        TextBlock valLblPush;
        valLblPush.Text(to_hstring(bufPush));
        valLblPush.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        valLblPush.FontSize(14.0);
        pushHeader.Children().Append(valLblPush);
        card.Children().Append(pushHeader);

        TextBlock pushDesc;
        pushDesc.Text(L"Horizontal parting distance in pixels for neighbor icons during drag (10 to 120 px).");
        pushDesc.FontSize(12.0);
        pushDesc.Opacity(0.7);
        card.Children().Append(pushDesc);

        drop_zone_push_slider->Minimum(10.0);
        drop_zone_push_slider->Maximum(120.0);
        drop_zone_push_slider->StepFrequency(5.0);
        drop_zone_push_slider->SmallChange(5.0);
        drop_zone_push_slider->LargeChange(20.0);
        drop_zone_push_slider->Value(cur_push);
        drop_zone_push_slider->IsThumbToolTipEnabled(true);
        drop_zone_push_slider->Width(460);
        drop_zone_push_slider->HorizontalAlignment(HorizontalAlignment::Left);
        drop_zone_push_slider->Margin(Thickness{ 0, 4, 0, 0 });

        NumberBox drop_zone_push_box;
        drop_zone_push_box.Width(95);
        drop_zone_push_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
        drop_zone_push_box.Minimum(10.0);
        drop_zone_push_box.Maximum(120.0);
        drop_zone_push_box.SmallChange(5.0);
        drop_zone_push_box.LargeChange(20.0);
        drop_zone_push_box.Value(cur_push);
        drop_zone_push_box.VerticalAlignment(VerticalAlignment::Center);

        auto is_updating_push = std::make_shared<bool>(false);

        auto debounce_push = std::make_shared<DispatcherTimer>();
        debounce_push->Interval(std::chrono::milliseconds(100));
        auto s_push = *drop_zone_push_slider;
        debounce_push->Tick([plugin_name, s_push, debounce_push](IInspectable const&, IInspectable const&) {
            debounce_push->Stop();
            SaveAndReload(plugin_name, "drop_zone_push", cJSON_CreateNumber(std::round(s_push.Value())));
        });

        drop_zone_push_slider->ValueChanged([valLblPush, drop_zone_push_box, is_updating_push, debounce_push](auto const&, auto const& e) {
            if (!*is_updating_push) {
                *is_updating_push = true;
                drop_zone_push_box.Value(std::round(e.NewValue()));
                *is_updating_push = false;
            }
            char b[32];
            snprintf(b, sizeof(b), "%d px", (int)std::round(e.NewValue()));
            valLblPush.Text(to_hstring(b));
            debounce_push->Stop();
            debounce_push->Start();
        });

        drop_zone_push_box.ValueChanged([valLblPush, drop_zone_push_slider, is_updating_push, debounce_push](auto const&, auto const& args) {
            if (!*is_updating_push && !std::isnan(args.NewValue())) {
                *is_updating_push = true;
                drop_zone_push_slider->Value(std::round(args.NewValue()));
                *is_updating_push = false;
                char b[32];
                snprintf(b, sizeof(b), "%d px", (int)std::round(args.NewValue()));
                valLblPush.Text(to_hstring(b));
                debounce_push->Stop();
                debounce_push->Start();
            }
        });

        drop_zone_push_slider->LostFocus([plugin_name, s_push, debounce_push](IInspectable const&, RoutedEventArgs const&) {
            debounce_push->Stop();
            SaveAndReload(plugin_name, "drop_zone_push", cJSON_CreateNumber(std::round(s_push.Value())));
        });

        StackPanel pushRow;
        pushRow.Orientation(Orientation::Horizontal);
        pushRow.Spacing(16);
        pushRow.Children().Append(*drop_zone_push_slider);
        pushRow.Children().Append(drop_zone_push_box);
        card.Children().Append(pushRow);

        panel.Children().Append(card);
    }

    // 10. Custom Start Button Image Section
    {
        StackPanel card;
        card.Spacing(8);
        card.Margin(Thickness{ 0, 0, 0, 20 });

        TextBlock secTitle;
        secTitle.Text(L"Custom Start Button Image");
        secTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        secTitle.FontSize(15.0);
        card.Children().Append(secTitle);

        TextBlock desc;
        desc.Text(L"Replace the Windows 11 Start button with a custom image (PNG, SVG, JPG, BMP, ICO). Fully participates in magnification and spring physics, and forwards clicks to open the Start menu.");
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        desc.TextWrapping(TextWrapping::Wrap);
        card.Children().Append(desc);

        std::string cur_img = GetStringSetting(plugin_name, "start_image_path", "Config/start_button.png");
        start_image_box->Text(to_hstring(cur_img));
        start_image_box->PlaceholderText(L"e.g. Config/start_button.png or start_button.svg");
        start_image_box->MinWidth(360);

        start_image_box->LostFocus([plugin_name](IInspectable const& sender, RoutedEventArgs const&) {
            std::string val = to_string(sender.as<TextBox>().Text());
            SaveAndReload(plugin_name, "start_image_path", cJSON_CreateString(val.c_str()));
        });

        Button browseBtn;
        browseBtn.Content(box_value(L"Browse..."));
        browseBtn.Padding(Thickness{ 16, 6, 16, 6 });

        Button resetBtn;
        resetBtn.Content(box_value(L"Reset"));
        resetBtn.Padding(Thickness{ 14, 6, 14, 6 });

        Button clearBtn;
        clearBtn.Content(box_value(L"Disable"));
        clearBtn.Padding(Thickness{ 14, 6, 14, 6 });

        auto box_ptr = start_image_box;
        browseBtn.Click([plugin_name, box_ptr](IInspectable const&, RoutedEventArgs const&) {
            std::wstring picked;
            if (PickImageFile(picked)) {
                for (auto& ch : picked) {
                    if (ch == L'\\') ch = L'/';
                }

                std::wstring cfg = ConfigIO_GetConfigPath();
                for (auto& ch : cfg) {
                    if (ch == L'\\') ch = L'/';
                }
                size_t c_pos = cfg.find(L"/Config/");
                if (c_pos != std::wstring::npos) {
                    std::wstring cfg_dir = cfg.substr(0, c_pos + 8);
                    if (picked.rfind(cfg_dir, 0) == 0) {
                        picked = L"Config/" + picked.substr(cfg_dir.length());
                    }
                }

                box_ptr->Text(hstring(picked));
                std::string picked_utf8 = to_string(box_ptr->Text());
                SaveAndReload(plugin_name, "start_image_path", cJSON_CreateString(picked_utf8.c_str()));
            }
        });

        resetBtn.Click([plugin_name, box_ptr](IInspectable const&, RoutedEventArgs const&) {
            box_ptr->Text(L"Config/start_button.png");
            SaveAndReload(plugin_name, "start_image_path", cJSON_CreateString("Config/start_button.png"));
        });

        clearBtn.Click([plugin_name, box_ptr](IInspectable const&, RoutedEventArgs const&) {
            box_ptr->Text(L"");
            SaveAndReload(plugin_name, "start_image_path", cJSON_CreateString(""));
        });

        StackPanel row;
        row.Orientation(Orientation::Horizontal);
        row.Spacing(10);
        row.Children().Append(*start_image_box);
        row.Children().Append(browseBtn);
        row.Children().Append(resetBtn);
        row.Children().Append(clearBtn);
        card.Children().Append(row);

        TextBlock note;
        note.Text(L"Note: Click 'Disable' or clear the path to restore the native Windows 11 Start button. Supports both relative and absolute paths.");
        note.FontSize(11.5);
        note.Opacity(0.5);
        card.Children().Append(note);

        panel.Children().Append(card);
    }

    // 11. Apply Button & Status Feedback
    {
        StackPanel actionPanel;
        actionPanel.Orientation(Orientation::Horizontal);
        actionPanel.Spacing(16);
        actionPanel.Margin(Thickness{ 0, 8, 0, 20 });

        Button applyBtn;
        applyBtn.Content(box_value(L"Apply Settings"));
        applyBtn.Padding(Thickness{ 24, 8, 24, 8 });

        TextBlock statusLbl;
        statusLbl.VerticalAlignment(VerticalAlignment::Center);
        statusLbl.FontSize(13.0);
        statusLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Medium());

        auto statusTimer = std::make_shared<DispatcherTimer>();
        statusTimer->Interval(std::chrono::seconds(3));
        statusTimer->Tick([statusLbl, statusTimer](IInspectable const&, IInspectable const&) {
            statusTimer->Stop();
            statusLbl.Text(L"");
        });

        applyBtn.Click([plugin_name, max_scale_slider, radius_slider, speed_slider, curve_combo, bounce_toggle, bounce_strength_slider, keep_on_top_toggle, tilt_toggle, tilt_angle_slider, drag_drop_toggle, drag_recession_slider, drop_zone_push_slider, start_image_box, statusLbl, statusTimer](IInspectable const&, RoutedEventArgs const&) {
            double scale = max_scale_slider->Value();
            int rad = (int)std::round(radius_slider->Value());
            int spd = (int)std::round(speed_slider->Value());
            int c_idx = curve_combo->SelectedIndex();
            const char* curves[] = { "gaussian", "cubic", "cosine", "linear" };
            const char* chosen_curve = (c_idx >= 0 && c_idx < 4) ? curves[c_idx] : "gaussian";
            bool bounce_en = bounce_toggle->IsOn();
            double b_strength = bounce_strength_slider->Value();
            bool keep_top = keep_on_top_toggle->IsOn();
            bool tilt_en = tilt_toggle->IsOn();
            double tilt_angle = std::round(tilt_angle_slider->Value());
            bool drag_en = drag_drop_toggle->IsOn();
            double drag_recession = drag_recession_slider->Value();
            double push_dist = std::round(drop_zone_push_slider->Value());
            std::string start_img = to_string(start_image_box->Text());

            std::vector<SettingKeyValue> items;
            items.push_back({ "max_scale", cJSON_CreateNumber(scale) });
            items.push_back({ "radius", cJSON_CreateNumber(rad) });
            items.push_back({ "speed_ms", cJSON_CreateNumber(spd) });
            items.push_back({ "curve", cJSON_CreateString(chosen_curve) });
            items.push_back({ "bounce_enabled", cJSON_CreateBool(bounce_en) });
            items.push_back({ "bounce_strength", cJSON_CreateNumber(b_strength) });
            items.push_back({ "keep_on_top", cJSON_CreateBool(keep_top) });
            items.push_back({ "tilt_enabled", cJSON_CreateBool(tilt_en) });
            items.push_back({ "max_tilt_angle", cJSON_CreateNumber(tilt_angle) });
            items.push_back({ "drag_drop_enabled", cJSON_CreateBool(drag_en) });
            items.push_back({ "drag_recession_scale", cJSON_CreateNumber(drag_recession) });
            items.push_back({ "drop_zone_push", cJSON_CreateNumber(push_dist) });
            items.push_back({ "start_image_path", cJSON_CreateString(start_img.c_str()) });

            SaveMultipleAndReload(plugin_name, items);

            statusLbl.Text(L"✓ Saved to default_config.jsonc and applied!");
            statusTimer->Stop();
            statusTimer->Start();
        });

        actionPanel.Children().Append(applyBtn);
        actionPanel.Children().Append(statusLbl);
        panel.Children().Append(actionPanel);
    }

    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

static Page CreateTaskbarResizeSettingsPage()
{
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(16);
    panel.Padding(Thickness{ 28, 24, 28, 28 });

    // Title
    TextBlock title;
    title.Text(L"Taskbar Resize Settings");
    title.FontSize(24.0);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    title.Margin(Thickness{ 0, 0, 0, 4 });
    panel.Children().Append(title);

    // Subtitle
    TextBlock subtitle;
    subtitle.Text(L"Adjust taskbar height, vertical padding offsets, and icon spacing.");
    subtitle.FontSize(13.0);
    subtitle.Opacity(0.7);
    subtitle.Margin(Thickness{ 0, 0, 0, 20 });
    panel.Children().Append(subtitle);

    const std::string plugin_name = "taskbar_resize";

    // 1. Enable Toggle
    {
        StackPanel card;
        card.Spacing(4);
        card.Margin(Thickness{ 0, 0, 0, 16 });

        ToggleSwitch toggle;
        toggle.Header(box_value(L"Enable Taskbar Resize"));
        toggle.OffContent(box_value(L"Disabled"));
        toggle.OnContent(box_value(L"Enabled"));
        bool enabled = GetBoolSetting(plugin_name, "enabled", true);
        toggle.IsOn(enabled);
        toggle.Toggled([plugin_name](IInspectable const& sender, RoutedEventArgs const&) {
            SaveAndReload(plugin_name, "enabled", cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
        });
        card.Children().Append(toggle);
        panel.Children().Append(card);
    }

    auto height_slider = std::make_shared<Slider>();
    auto pad_top_slider = std::make_shared<Slider>();
    auto pad_bot_slider = std::make_shared<Slider>();
    auto spacing_slider = std::make_shared<Slider>();

    auto add_int_slider = [&](const std::wstring& title_str, const std::wstring& desc_str,
                              const std::string& key, double min_v, double max_v, double def_v,
                              std::shared_ptr<Slider> slider_ptr) {
        StackPanel card;
        card.Spacing(4);
        card.Margin(Thickness{ 0, 0, 0, 16 });

        StackPanel header;
        header.Orientation(Orientation::Horizontal);
        header.Spacing(12);

        TextBlock lbl;
        lbl.Text(title_str);
        lbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        lbl.FontSize(14.0);
        header.Children().Append(lbl);

        double cur_val = GetDoubleSetting(plugin_name, key, def_v);
        if (cur_val < min_v) cur_val = min_v;
        if (cur_val > max_v) cur_val = max_v;

        char buf[32];
        snprintf(buf, sizeof(buf), "%d px", (int)std::round(cur_val));
        TextBlock valLbl;
        valLbl.Text(to_hstring(buf));
        valLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        valLbl.FontSize(14.0);
        header.Children().Append(valLbl);
        card.Children().Append(header);

        TextBlock desc;
        desc.Text(desc_str);
        desc.FontSize(12.0);
        desc.Opacity(0.7);
        card.Children().Append(desc);

        slider_ptr->Minimum(min_v);
        slider_ptr->Maximum(max_v);
        slider_ptr->StepFrequency(1.0);
        slider_ptr->SmallChange(1.0);
        slider_ptr->LargeChange(4.0);
        slider_ptr->Value(cur_val);
        slider_ptr->IsThumbToolTipEnabled(true);
        slider_ptr->Width(460);
        slider_ptr->HorizontalAlignment(HorizontalAlignment::Left);
        slider_ptr->Margin(Thickness{ 0, 4, 0, 0 });

        NumberBox num_box;
        num_box.Width(95);
        num_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
        num_box.Minimum(min_v);
        num_box.Maximum(max_v);
        num_box.SmallChange(1.0);
        num_box.LargeChange(4.0);
        num_box.Value(cur_val);
        num_box.VerticalAlignment(VerticalAlignment::Center);

        auto is_updating = std::make_shared<bool>(false);

        auto debounce = std::make_shared<DispatcherTimer>();
        debounce->Interval(std::chrono::milliseconds(100));
        auto s = *slider_ptr;
        debounce->Tick([plugin_name, key, s, debounce](IInspectable const&, IInspectable const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, key, cJSON_CreateNumber(std::round(s.Value())));
        });

        slider_ptr->ValueChanged([valLbl, num_box, is_updating, debounce](auto const&, auto const& e) {
            if (!*is_updating) {
                *is_updating = true;
                num_box.Value(std::round(e.NewValue()));
                *is_updating = false;
            }
            char b[32];
            snprintf(b, sizeof(b), "%d px", (int)std::round(e.NewValue()));
            valLbl.Text(to_hstring(b));
            debounce->Stop();
            debounce->Start();
        });

        num_box.ValueChanged([valLbl, slider_ptr, is_updating, debounce](auto const&, NumberBoxValueChangedEventArgs const& args) {
            if (!*is_updating && !std::isnan(args.NewValue())) {
                *is_updating = true;
                slider_ptr->Value(std::round(args.NewValue()));
                *is_updating = false;
                char b[32];
                snprintf(b, sizeof(b), "%d px", (int)std::round(args.NewValue()));
                valLbl.Text(to_hstring(b));
                debounce->Stop();
                debounce->Start();
            }
        });

        slider_ptr->LostFocus([plugin_name, key, s, debounce](IInspectable const&, RoutedEventArgs const&) {
            debounce->Stop();
            SaveAndReload(plugin_name, key, cJSON_CreateNumber(std::round(s.Value())));
        });

        StackPanel controlRow;
        controlRow.Orientation(Orientation::Horizontal);
        controlRow.Spacing(16);
        controlRow.Children().Append(*slider_ptr);
        controlRow.Children().Append(num_box);
        card.Children().Append(controlRow);
        panel.Children().Append(card);
    };

    add_int_slider(L"Taskbar Height (height)", L"Height in logical pixels (default: 32 sweet spot, 36 relaxed, 24 compact).", "height", 20, 128, 32, height_slider);
    add_int_slider(L"Top Padding Offset (padding_top)", L"Fine-tune vertical icon alignment from top in logical pixels.", "padding_top", -20, 20, 0, pad_top_slider);
    add_int_slider(L"Bottom Padding Offset (padding_bottom)", L"Fine-tune vertical icon alignment from bottom in logical pixels.", "padding_bottom", -20, 20, 0, pad_bot_slider);
    add_int_slider(L"Icon Spacing (icon_spacing)", L"Horizontal spacing between icons in logical pixels.", "icon_spacing", 0, 32, 4, spacing_slider);

    // Apply Button & Status Feedback
    {
        StackPanel actionPanel;
        actionPanel.Orientation(Orientation::Horizontal);
        actionPanel.Spacing(16);
        actionPanel.Margin(Thickness{ 0, 8, 0, 20 });

        Button applyBtn;
        applyBtn.Content(box_value(L"Apply Settings"));
        applyBtn.Padding(Thickness{ 24, 8, 24, 8 });

        TextBlock statusLbl;
        statusLbl.VerticalAlignment(VerticalAlignment::Center);
        statusLbl.FontSize(13.0);
        statusLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Medium());

        auto statusTimer = std::make_shared<DispatcherTimer>();
        statusTimer->Interval(std::chrono::seconds(3));
        statusTimer->Tick([statusLbl, statusTimer](IInspectable const&, IInspectable const&) {
            statusTimer->Stop();
            statusLbl.Text(L"");
        });

        applyBtn.Click([plugin_name, height_slider, pad_top_slider, pad_bot_slider, spacing_slider, statusLbl, statusTimer](IInspectable const&, RoutedEventArgs const&) {
            int h = (int)std::round(height_slider->Value());
            int pt = (int)std::round(pad_top_slider->Value());
            int pb = (int)std::round(pad_bot_slider->Value());
            int sp = (int)std::round(spacing_slider->Value());

            std::vector<SettingKeyValue> items;
            items.push_back({ "height", cJSON_CreateNumber(h) });
            items.push_back({ "padding_top", cJSON_CreateNumber(pt) });
            items.push_back({ "padding_bottom", cJSON_CreateNumber(pb) });
            items.push_back({ "icon_spacing", cJSON_CreateNumber(sp) });

            SaveMultipleAndReload(plugin_name, items);

            statusLbl.Text(L"✓ Saved to default_config.jsonc and applied!");
            statusTimer->Stop();
            statusTimer->Start();
        });

        actionPanel.Children().Append(applyBtn);
        actionPanel.Children().Append(statusLbl);
        panel.Children().Append(actionPanel);
    }

    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

Page CreateSettingsPage(const std::string& plugin_name, const std::string& schema_json)
{
    if (plugin_name == "icon_hover") {
        return CreateIconHoverSettingsPage();
    }
    if (plugin_name == "taskbar_resize") {
        return CreateTaskbarResizeSettingsPage();
    }

    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(16);
    panel.Padding(Thickness{ 24, 24, 24, 24 });
    
    TextBlock title;
    title.Text(to_hstring(plugin_name + " Settings"));
    title.FontSize(24.0);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    title.Margin(Thickness{ 0, 0, 0, 24 });
    panel.Children().Append(title);

    cJSON* schema = cJSON_Parse(schema_json.c_str());
    if (schema) {
        cJSON* settings = cJSON_GetObjectItem(schema, "settings");
        if (settings && cJSON_IsArray(settings)) {
            cJSON* setting = nullptr;
            cJSON_ArrayForEach(setting, settings) {
                cJSON* c_key = cJSON_GetObjectItem(setting, "key");
                cJSON* c_label = cJSON_GetObjectItem(setting, "label");
                cJSON* c_type = cJSON_GetObjectItem(setting, "type");
                
                if (!c_key || !cJSON_IsString(c_key) || !c_label || !cJSON_IsString(c_label) || !c_type || !cJSON_IsString(c_type)) continue;
                
                std::string key = c_key->valuestring;
                std::string label = c_label->valuestring;
                std::string type = c_type->valuestring;
                
                StackPanel itemPanel;
                itemPanel.Spacing(8);
                
                TextBlock lbl;
                lbl.Text(to_hstring(label));
                lbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                itemPanel.Children().Append(lbl);
                
                cJSON* tooltip = cJSON_GetObjectItem(setting, "tooltip");
                if (tooltip && cJSON_IsString(tooltip) && tooltip->valuestring) {
                    ToolTipService::SetToolTip(lbl, box_value(to_hstring(tooltip->valuestring)));
                }
                
                cJSON* current = GetCurrentValue(plugin_name, key);
                
                if (type == "bool") {
                    ToggleSwitch toggle;
                    toggle.OffContent(box_value(L"Off"));
                    toggle.OnContent(box_value(L"On"));
                    
                    bool val = false;
                    if (current && cJSON_IsBool(current)) val = cJSON_IsTrue(current);
                    else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) val = cJSON_IsTrue(def);
                    
                    toggle.IsOn(val);
                    toggle.Toggled([plugin_name, key](IInspectable const& sender, RoutedEventArgs const&) {
                        SaveAndReload(plugin_name, key, cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
                    });
                    itemPanel.Children().Append(toggle);
                } else if (type == "int" || type == "float" || type == "slider") {
                    double min_val = 0;
                    double max_val = 100;
                    double step_val = (type == "float") ? 0.1 : 1.0;
                    if (cJSON* min = cJSON_GetObjectItem(setting, "min")) {
                        if (cJSON_IsNumber(min)) min_val = min->valuedouble;
                    }
                    if (cJSON* max = cJSON_GetObjectItem(setting, "max")) {
                        if (cJSON_IsNumber(max)) max_val = max->valuedouble;
                    }
                    if (cJSON* step = cJSON_GetObjectItem(setting, "step")) {
                        if (cJSON_IsNumber(step)) step_val = step->valuedouble;
                    }

                    double val = 0;
                    if (current && cJSON_IsNumber(current)) val = current->valuedouble;
                    else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) val = def->valuedouble;

                    Slider slider;
                    slider.Minimum(min_val);
                    slider.Maximum(max_val);
                    slider.StepFrequency(step_val);
                    slider.SmallChange(step_val);
                    slider.Value(val);
                    slider.IsThumbToolTipEnabled(true);
                    slider.MaxWidth(450);
                    slider.HorizontalAlignment(HorizontalAlignment::Left);

                    slider.ValueChanged([plugin_name, key, type](auto const&, auto const& args) {
                        if (std::isnan(args.NewValue())) return;
                        if (type == "int") {
                            SaveAndReload(plugin_name, key, cJSON_CreateNumber(std::round(args.NewValue())));
                        } else {
                            SaveAndReload(plugin_name, key, cJSON_CreateNumber(args.NewValue()));
                        }
                    });
                    itemPanel.Children().Append(slider);
                } else if (type == "enum") {
                    ComboBox combo;
                    cJSON* options = cJSON_GetObjectItem(setting, "options");
                    
                    std::string val_str = "";
                    if (current && cJSON_IsString(current)) val_str = current->valuestring;
                    else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) val_str = def->valuestring;
                    
                    int selected_idx = 0;
                    int i = 0;
                    std::vector<std::string> opt_strings;
                    
                    if (options && cJSON_IsArray(options)) {
                        cJSON* opt = nullptr;
                        cJSON_ArrayForEach(opt, options) {
                            if (cJSON_IsString(opt)) {
                                combo.Items().Append(box_value(to_hstring(opt->valuestring)));
                                opt_strings.push_back(opt->valuestring);
                                if (val_str == opt->valuestring) selected_idx = i;
                                i++;
                            }
                        }
                    }
                    combo.SelectedIndex(selected_idx);
                    combo.SelectionChanged([plugin_name, key, opt_strings](IInspectable const& sender, SelectionChangedEventArgs const&) {
                        int idx = sender.as<ComboBox>().SelectedIndex();
                        if (idx >= 0 && (size_t)idx < opt_strings.size()) {
                            SaveAndReload(plugin_name, key, cJSON_CreateString(opt_strings[idx].c_str()));
                        }
                    });
                    itemPanel.Children().Append(combo);
                } else if (type == "string") {
                    TextBox txt;
                    std::string val_str = "";
                    if (current && cJSON_IsString(current)) val_str = current->valuestring;
                    else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) val_str = def->valuestring;
                    
                    txt.Text(to_hstring(val_str));
                    txt.LostFocus([plugin_name, key](IInspectable const& sender, RoutedEventArgs const&) {
                        SaveAndReload(plugin_name, key, cJSON_CreateString(to_string(sender.as<TextBox>().Text()).c_str()));
                    });
                    itemPanel.Children().Append(txt);
                } else if (type == "color") {
                    ColorPicker picker;
                    picker.IsColorSpectrumVisible(true);
                    picker.IsAlphaEnabled(true);
                    picker.IsHexInputVisible(true);
                    
                    uint32_t val_color = 0xFFFFFFFF;
                    if (current && cJSON_IsNumber(current)) {
                        val_color = (uint32_t)current->valuedouble;
                    } else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) {
                        if (cJSON_IsNumber(def)) val_color = (uint32_t)def->valuedouble;
                    }
                    
                    winrt::Windows::UI::Color c;
                    c.A = static_cast<uint8_t>((val_color >> 24) & 0xFF);
                    c.R = static_cast<uint8_t>((val_color >> 16) & 0xFF);
                    c.G = static_cast<uint8_t>((val_color >> 8) & 0xFF);
                    c.B = static_cast<uint8_t>(val_color & 0xFF);
                    picker.Color(c);
                    
                    picker.ColorChanged([plugin_name, key](ColorPicker const&, ColorChangedEventArgs const& args) {
                        auto clr = args.NewColor();
                        uint32_t argb = ((uint32_t)clr.A << 24) | ((uint32_t)clr.R << 16) | ((uint32_t)clr.G << 8) | (uint32_t)clr.B;
                        SaveAndReload(plugin_name, key, cJSON_CreateNumber(argb));
                    });
                    itemPanel.Children().Append(picker);
                }

                if (current) cJSON_Delete(current);
                panel.Children().Append(itemPanel);
            }
        }
        cJSON_Delete(schema);
    }
    
    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

}
