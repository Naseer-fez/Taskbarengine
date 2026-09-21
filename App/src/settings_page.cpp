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
#include <winrt/Microsoft.UI.Xaml.Media.h>
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
using namespace winrt::Microsoft::UI::Xaml::Media;

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

static cJSON* GetDynamicIslandSubValue(const std::string& key)
{
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    std::wstring path = ConfigIO_GetConfigPath();
    cJSON* root = ConfigIO_Load(path);
    cJSON* res = nullptr;
    if (root) {
        cJSON* icon_hover = ConfigIO_GetPluginValue(root, "icon_hover", "dynamic_island");
        if (icon_hover && cJSON_IsObject(icon_hover)) {
            cJSON* val = cJSON_GetObjectItemCaseSensitive(icon_hover, key.c_str());
            if (val) {
                res = cJSON_Duplicate(val, 1);
            }
        }
        cJSON_Delete(root);
    }
    return res;
}

static double GetDynamicIslandDouble(const std::string& key, double default_val)
{
    cJSON* node = GetDynamicIslandSubValue(key);
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

static bool GetDynamicIslandBool(const std::string& key, bool default_val)
{
    cJSON* node = GetDynamicIslandSubValue(key);
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

static void SaveDynamicIslandSetting(const std::string& key, cJSON* value)
{
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    std::wstring path = ConfigIO_GetConfigPath();
    cJSON* root = ConfigIO_Load(path);
    if (!root) {
        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            if (value) cJSON_Delete(value);
            return;
        }
        root = cJSON_CreateObject();
    }

    cJSON* plugins = cJSON_GetObjectItemCaseSensitive(root, "plugins");
    if (!plugins || !cJSON_IsObject(plugins)) {
        if (!plugins) {
            plugins = cJSON_CreateObject();
            if (plugins) cJSON_AddItemToObject(root, "plugins", plugins);
        }
    }

    cJSON* icon_hover = plugins ? cJSON_GetObjectItemCaseSensitive(plugins, "icon_hover") : nullptr;
    if (!icon_hover || !cJSON_IsObject(icon_hover)) {
        if (!icon_hover && plugins) {
            icon_hover = cJSON_CreateObject();
            if (icon_hover) cJSON_AddItemToObject(plugins, "icon_hover", icon_hover);
        }
    }

    cJSON* di = icon_hover ? cJSON_GetObjectItemCaseSensitive(icon_hover, "dynamic_island") : nullptr;
    if (!di || !cJSON_IsObject(di)) {
        if (!di && icon_hover) {
            di = cJSON_CreateObject();
            if (di) cJSON_AddItemToObject(icon_hover, "dynamic_island", di);
        }
    }

    if (di && value) {
        cJSON* existing = cJSON_GetObjectItemCaseSensitive(di, key.c_str());
        if (existing) {
            cJSON_ReplaceItemInObjectCaseSensitive(di, key.c_str(), value);
        } else {
            cJSON_AddItemToObject(di, key.c_str(), value);
        }

        if (SUCCEEDED(ConfigIO_Save(path, root))) {
            GuiIpcReloadConfig();
        }
    } else {
        if (value) cJSON_Delete(value);
    }

    cJSON_Delete(root);
}

static void SaveDynamicIslandMultiple(const std::vector<SettingKeyValue>& items)
{
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    std::wstring path = ConfigIO_GetConfigPath();
    cJSON* root = ConfigIO_Load(path);
    if (!root) {
        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            for (const auto& it : items) {
                if (it.value) cJSON_Delete(it.value);
            }
            return;
        }
        root = cJSON_CreateObject();
    }

    cJSON* plugins = cJSON_GetObjectItemCaseSensitive(root, "plugins");
    if (!plugins || !cJSON_IsObject(plugins)) {
        if (!plugins) {
            plugins = cJSON_CreateObject();
            if (plugins) cJSON_AddItemToObject(root, "plugins", plugins);
        }
    }

    cJSON* icon_hover = plugins ? cJSON_GetObjectItemCaseSensitive(plugins, "icon_hover") : nullptr;
    if (!icon_hover || !cJSON_IsObject(icon_hover)) {
        if (!icon_hover && plugins) {
            icon_hover = cJSON_CreateObject();
            if (icon_hover) cJSON_AddItemToObject(plugins, "icon_hover", icon_hover);
        }
    }

    cJSON* di = icon_hover ? cJSON_GetObjectItemCaseSensitive(icon_hover, "dynamic_island") : nullptr;
    if (!di || !cJSON_IsObject(di)) {
        if (!di && icon_hover) {
            di = cJSON_CreateObject();
            if (di) cJSON_AddItemToObject(icon_hover, "dynamic_island", di);
        }
    }

    if (di) {
        for (const auto& it : items) {
            cJSON* existing = cJSON_GetObjectItemCaseSensitive(di, it.key.c_str());
            if (existing) {
                cJSON_ReplaceItemInObjectCaseSensitive(di, it.key.c_str(), it.value);
            } else {
                cJSON_AddItemToObject(di, it.key.c_str(), it.value);
            }
        }

        if (SUCCEEDED(ConfigIO_Save(path, root))) {
            GuiIpcReloadConfig();
        }
    } else {
        for (const auto& it : items) {
            if (it.value) cJSON_Delete(it.value);
        }
    }

    cJSON_Delete(root);
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
    HWND owner = GetActiveWindow();
    if (!owner) owner = GetForegroundWindow();
    ofn.hwndOwner = owner;
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

// ============================================================================
// Windows 11 Fluent UI Layout & Control Helpers
// ============================================================================

static StackPanel CreatePageHeader(const std::wstring& title_str, const std::wstring& subtitle_str)
{
    StackPanel header;
    header.Spacing(4);
    header.Margin(Thickness{ 0, 0, 0, 16 });

    TextBlock title;
    title.Text(title_str);
    title.FontSize(26.0);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    header.Children().Append(title);

    TextBlock subtitle;
    subtitle.Text(subtitle_str);
    subtitle.FontSize(13.0);
    subtitle.Opacity(0.72);
    subtitle.TextWrapping(TextWrapping::Wrap);
    header.Children().Append(subtitle);

    return header;
}

static TextBlock CreateSectionHeader(const std::wstring& sec_title)
{
    TextBlock header;
    header.Text(sec_title);
    header.FontSize(15.0);
    header.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    header.Margin(Thickness{ 0, 14, 0, 6 });
    return header;
}

static Border CreateSettingsCard(const std::wstring& title, const std::wstring& desc, FrameworkElement const& control, bool vertical_layout = false)
{
    Border card;
    card.CornerRadius(CornerRadius{ 4, 4, 4, 4 });
    card.BorderThickness(Thickness{ 1, 1, 1, 1 });
    card.Margin(Thickness{ 0, 0, 0, 8 });
    card.Padding(Thickness{ 16, 12, 16, 12 });

    try {
        auto res = Application::Current().Resources();
        if (res.HasKey(box_value(L"CardBackgroundFillColorDefaultBrush"))) {
            card.Background(res.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>());
        }
        if (res.HasKey(box_value(L"CardStrokeColorDefaultBrush"))) {
            card.BorderBrush(res.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>());
        }
    } catch (...) {}

    if (vertical_layout) {
        StackPanel vPanel;
        vPanel.Spacing(8);

        TextBlock titleBlock;
        titleBlock.Text(title);
        titleBlock.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        titleBlock.FontSize(14.0);
        vPanel.Children().Append(titleBlock);

        if (!desc.empty()) {
            TextBlock descBlock;
            descBlock.Text(desc);
            descBlock.FontSize(12.0);
            descBlock.Opacity(0.72);
            descBlock.TextWrapping(TextWrapping::Wrap);
            vPanel.Children().Append(descBlock);
        }

        vPanel.Children().Append(control);
        card.Child(vPanel);
    } else {
        Grid grid;
        ColumnDefinition col1;
        col1.Width(GridLength{ 1.0, GridUnitType::Star });
        ColumnDefinition col2;
        col2.Width(GridLength{ 0.0, GridUnitType::Auto });
        grid.ColumnDefinitions().Append(col1);
        grid.ColumnDefinitions().Append(col2);

        StackPanel textPanel;
        textPanel.Spacing(2);
        textPanel.VerticalAlignment(VerticalAlignment::Center);
        textPanel.Margin(Thickness{ 0, 0, 16, 0 });

        TextBlock titleBlock;
        titleBlock.Text(title);
        titleBlock.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        titleBlock.FontSize(14.0);
        textPanel.Children().Append(titleBlock);

        if (!desc.empty()) {
            TextBlock descBlock;
            descBlock.Text(desc);
            descBlock.FontSize(12.0);
            descBlock.Opacity(0.72);
            descBlock.TextWrapping(TextWrapping::Wrap);
            textPanel.Children().Append(descBlock);
        }

        Grid::SetColumn(textPanel, 0);
        grid.Children().Append(textPanel);

        Grid::SetColumn(control, 1);
        grid.Children().Append(control);

        card.Child(grid);
    }
    return card;
}

struct PairedSliderControl {
    Border card;
    std::shared_ptr<Slider> slider;
    std::shared_ptr<NumberBox> numberBox;
    TextBlock valLbl;
    std::function<void(double)> set_value;
};

static PairedSliderControl CreateSliderSettingCard(
    const std::wstring& title,
    const std::wstring& desc,
    const std::string& plugin_name,
    const std::string& key,
    double min_v, double max_v, double step_v, double def_v,
    const std::wstring& unit_str,
    int decimal_places = 0
) {
    auto slider = std::make_shared<Slider>();
    auto numBox = std::make_shared<NumberBox>();

    double cur_val = GetDoubleSetting(plugin_name, key, def_v);
    if (cur_val < min_v) cur_val = min_v;
    if (cur_val > max_v) cur_val = max_v;

    TextBlock valLbl;
    valLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
    valLbl.FontSize(13.5);
    valLbl.VerticalAlignment(VerticalAlignment::Center);

    auto format_val = [unit_str, decimal_places](double v) -> hstring {
        char buf[64];
        if (decimal_places > 0) {
            snprintf(buf, sizeof(buf), "%.*f", decimal_places, v);
        } else {
            snprintf(buf, sizeof(buf), "%d", (int)std::round(v));
        }
        std::wstring ws = to_hstring(buf).c_str();
        if (!unit_str.empty()) {
            ws += L" " + unit_str;
        }
        return hstring(ws);
    };

    valLbl.Text(format_val(cur_val));

    slider->Minimum(min_v);
    slider->Maximum(max_v);
    slider->StepFrequency(step_v);
    slider->SmallChange(step_v);
    slider->LargeChange(step_v * 4.0);
    slider->Value(cur_val);
    slider->IsThumbToolTipEnabled(true);
    slider->Width(260);
    slider->VerticalAlignment(VerticalAlignment::Center);

    numBox->Width(90);
    numBox->SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
    numBox->Minimum(min_v);
    numBox->Maximum(max_v);
    numBox->SmallChange(step_v);
    numBox->LargeChange(step_v * 4.0);
    numBox->Value(cur_val);
    numBox->VerticalAlignment(VerticalAlignment::Center);

    auto is_updating = std::make_shared<bool>(false);
    auto debounce = std::make_shared<DispatcherTimer>();
    debounce->Interval(std::chrono::milliseconds(100));

    auto s = *slider;
    debounce->Tick([plugin_name, key, s, debounce, decimal_places](IInspectable const&, IInspectable const&) {
        try {
            debounce->Stop();
            double val = s.Value();
            if (decimal_places == 0) val = std::round(val);
            SaveAndReload(plugin_name, key, cJSON_CreateNumber(val));
        } catch (...) {}
    });

    slider->ValueChanged([valLbl, numBox, is_updating, debounce, format_val](auto const&, auto const& e) {
        if (!*is_updating) {
            *is_updating = true;
            numBox->Value(e.NewValue());
            *is_updating = false;
            debounce->Stop();
            debounce->Start();
        }
        valLbl.Text(format_val(e.NewValue()));
    });

    numBox->ValueChanged([valLbl, slider, is_updating, debounce, format_val](auto const&, NumberBoxValueChangedEventArgs const& args) {
        if (!*is_updating && !std::isnan(args.NewValue())) {
            *is_updating = true;
            slider->Value(args.NewValue());
            *is_updating = false;
            valLbl.Text(format_val(args.NewValue()));
            debounce->Stop();
            debounce->Start();
        }
    });

    slider->LostFocus([plugin_name, key, s, debounce, decimal_places](IInspectable const&, RoutedEventArgs const&) {
        try {
            debounce->Stop();
            double val = s.Value();
            if (decimal_places == 0) val = std::round(val);
            SaveAndReload(plugin_name, key, cJSON_CreateNumber(val));
        } catch (...) {}
    });

    auto set_value_fn = [slider, numBox, valLbl, debounce, is_updating, format_val](double v) {
        *is_updating = true;
        debounce->Stop();
        slider->Value(v);
        numBox->Value(v);
        valLbl.Text(format_val(v));
        *is_updating = false;
    };

    StackPanel rightPanel;
    rightPanel.Orientation(Orientation::Horizontal);
    rightPanel.Spacing(12);
    rightPanel.VerticalAlignment(VerticalAlignment::Center);
    rightPanel.Children().Append(valLbl);
    rightPanel.Children().Append(*slider);
    rightPanel.Children().Append(*numBox);

    Border card = CreateSettingsCard(title, desc, rightPanel);
    return { card, slider, numBox, valLbl, set_value_fn };
}

static PairedSliderControl CreateDynamicIslandSliderSettingCard(
    const std::wstring& title,
    const std::wstring& desc,
    const std::string& key,
    double min_v, double max_v, double step_v, double def_v,
    const std::wstring& unit_str
) {
    auto slider = std::make_shared<Slider>();
    auto numBox = std::make_shared<NumberBox>();

    double cur_val = GetDynamicIslandDouble(key, def_v);
    if (cur_val < min_v) cur_val = min_v;
    if (cur_val > max_v) cur_val = max_v;

    TextBlock valLbl;
    valLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
    valLbl.FontSize(13.5);
    valLbl.VerticalAlignment(VerticalAlignment::Center);

    auto format_val = [unit_str](double v) -> hstring {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d %s", (int)std::round(v), to_string(unit_str).c_str());
        return to_hstring(buf);
    };

    valLbl.Text(format_val(cur_val));

    slider->Minimum(min_v);
    slider->Maximum(max_v);
    slider->StepFrequency(step_v);
    slider->SmallChange(step_v);
    slider->LargeChange(step_v * 4.0);
    slider->Value(cur_val);
    slider->IsThumbToolTipEnabled(true);
    slider->Width(260);
    slider->VerticalAlignment(VerticalAlignment::Center);

    numBox->Width(90);
    numBox->SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
    numBox->Minimum(min_v);
    numBox->Maximum(max_v);
    numBox->SmallChange(step_v);
    numBox->LargeChange(step_v * 4.0);
    numBox->Value(cur_val);
    numBox->VerticalAlignment(VerticalAlignment::Center);

    auto is_updating = std::make_shared<bool>(false);
    auto debounce = std::make_shared<DispatcherTimer>();
    debounce->Interval(std::chrono::milliseconds(100));

    slider->ValueChanged([valLbl, numBox, is_updating, debounce, format_val](auto const&, auto const& e) {
        if (!*is_updating) {
            *is_updating = true;
            numBox->Value(e.NewValue());
            *is_updating = false;
            debounce->Stop();
            debounce->Start();
        }
        valLbl.Text(format_val(e.NewValue()));
    });

    numBox->ValueChanged([valLbl, slider, is_updating, debounce, format_val](auto const&, NumberBoxValueChangedEventArgs const& args) {
        if (!*is_updating && !std::isnan(args.NewValue())) {
            *is_updating = true;
            slider->Value(args.NewValue());
            *is_updating = false;
            valLbl.Text(format_val(args.NewValue()));
            debounce->Stop();
            debounce->Start();
        }
    });

    auto s = *slider;
    debounce->Tick([key, s, debounce](IInspectable const&, IInspectable const&) {
        try {
            debounce->Stop();
            SaveDynamicIslandSetting(key, cJSON_CreateNumber(std::round(s.Value())));
        } catch (...) {}
    });

    slider->LostFocus([key, s, debounce](IInspectable const&, RoutedEventArgs const&) {
        try {
            debounce->Stop();
            SaveDynamicIslandSetting(key, cJSON_CreateNumber(std::round(s.Value())));
        } catch (...) {}
    });

    auto set_value_fn = [slider, numBox, valLbl, debounce, is_updating, format_val](double v) {
        *is_updating = true;
        debounce->Stop();
        slider->Value(v);
        numBox->Value(v);
        valLbl.Text(format_val(v));
        *is_updating = false;
    };

    StackPanel rightPanel;
    rightPanel.Orientation(Orientation::Horizontal);
    rightPanel.Spacing(12);
    rightPanel.VerticalAlignment(VerticalAlignment::Center);
    rightPanel.Children().Append(valLbl);
    rightPanel.Children().Append(*slider);
    rightPanel.Children().Append(*numBox);

    Border card = CreateSettingsCard(title, desc, rightPanel);
    return { card, slider, numBox, valLbl, set_value_fn };
}

static Border CreateToggleSettingCard(
    const std::wstring& title,
    const std::wstring& desc,
    const std::string& plugin_name,
    const std::string& key,
    bool def_v,
    std::shared_ptr<ToggleSwitch> out_toggle = nullptr
) {
    ToggleSwitch toggle;
    if (out_toggle) toggle = *out_toggle;
    toggle.OffContent(box_value(L"Off"));
    toggle.OnContent(box_value(L"On"));
    bool cur = GetBoolSetting(plugin_name, key, def_v);
    toggle.IsOn(cur);
    toggle.VerticalAlignment(VerticalAlignment::Center);

    toggle.Toggled([plugin_name, key](IInspectable const& sender, RoutedEventArgs const&) {
        bool is_on = sender.as<ToggleSwitch>().IsOn();
        SaveAndReload(plugin_name, key, cJSON_CreateBool(is_on));
        if (key == "enabled") {
            if (is_on) {
                GuiIpcEnablePlugin(plugin_name);
            } else {
                GuiIpcDisablePlugin(plugin_name);
            }
        }
    });

    return CreateSettingsCard(title, desc, toggle);
}

struct ActionBarControls {
    StackPanel panel;
    Button applyBtn;
    TextBlock statusLbl;
};

static ActionBarControls CreateActionBar(const std::wstring& btn_text = L"Apply Changes")
{
    StackPanel actionPanel;
    actionPanel.Orientation(Orientation::Horizontal);
    actionPanel.Spacing(16);
    actionPanel.Margin(Thickness{ 0, 16, 0, 24 });

    Button applyBtn;
    applyBtn.Content(box_value(btn_text));
    applyBtn.Padding(Thickness{ 20, 8, 20, 8 });
    applyBtn.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

    TextBlock statusLbl;
    statusLbl.VerticalAlignment(VerticalAlignment::Center);
    statusLbl.FontSize(13.0);
    statusLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::Medium());

    actionPanel.Children().Append(applyBtn);
    actionPanel.Children().Append(statusLbl);

    return { actionPanel, applyBtn, statusLbl };
}

static void ShowTimedStatus(TextBlock const& lbl, const std::wstring& message, int seconds = 3)
{
    lbl.Text(message);
    auto timer = std::make_shared<DispatcherTimer>();
    timer->Interval(std::chrono::seconds(seconds));
    timer->Tick([lbl, timer](IInspectable const&, IInspectable const&) {
        try {
            timer->Stop();
            lbl.Text(L"");
        } catch (...) {}
    });
    timer->Start();
}

// ============================================================================
// 1. Taskbar Resize Page (Includes "Default Taskbar" feature)
// ============================================================================

Page CreateTaskbarResizePage()
{
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(8);
    panel.Padding(Thickness{ 28, 24, 28, 28 });

    panel.Children().Append(CreatePageHeader(
        L"Taskbar Resize & Geometry",
        L"Adjust taskbar height, vertical alignment offsets, icon density, or instantly restore original Windows factory dimensions."
    ));

    const std::string plugin_name = "taskbar_resize";

    // 1. Master Toggle Card
    auto masterToggle = std::make_shared<ToggleSwitch>();
    panel.Children().Append(CreateToggleSettingCard(
        L"Enable Taskbar Resize Subsystem",
        L"When enabled, dynamically adjusts taskbar height and synchronizes the desktop work area via DirectComposition and SPI_SETWORKAREA.",
        plugin_name, "enabled", true, masterToggle
    ));

    panel.Children().Append(CreateSectionHeader(L"Default Taskbar & Presets"));

    // 2. The "Default Taskbar" Option Card
    Button defaultTaskbarBtn;
    defaultTaskbarBtn.Content(box_value(L"Restore Default Taskbar"));
    defaultTaskbarBtn.Padding(Thickness{ 16, 8, 16, 8 });
    defaultTaskbarBtn.VerticalAlignment(VerticalAlignment::Center);
    defaultTaskbarBtn.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

    Border defaultCard = CreateSettingsCard(
        L"Default Taskbar (Windows 11 Standard - 48 px)",
        L"Reverts the taskbar to Windows original factory dimensions (48 px height, 0 px padding). Ensures that maximized applications and full-screen modes completely cover the desktop background without gaps or misalignments.",
        defaultTaskbarBtn
    );
    panel.Children().Append(defaultCard);

    // 3. Quick Size Presets Card
    StackPanel presetsRow;
    presetsRow.Orientation(Orientation::Horizontal);
    presetsRow.Spacing(10);
    presetsRow.VerticalAlignment(VerticalAlignment::Center);

    Button presetDefault;
    presetDefault.Content(box_value(L"Default (48px)"));
    presetDefault.Padding(Thickness{ 14, 6, 14, 6 });

    Button presetCompact;
    presetCompact.Content(box_value(L"Compact (32px)"));
    presetCompact.Padding(Thickness{ 14, 6, 14, 6 });

    Button presetUltra;
    presetUltra.Content(box_value(L"Ultra-Compact (24px)"));
    presetUltra.Padding(Thickness{ 14, 6, 14, 6 });

    presetsRow.Children().Append(presetDefault);
    presetsRow.Children().Append(presetCompact);
    presetsRow.Children().Append(presetUltra);

    panel.Children().Append(CreateSettingsCard(
        L"Quick Sizing Presets",
        L"Apply recommended taskbar height and padding configurations with a single click.",
        presetsRow
    ));

    // 4. Sliders for Custom Dimensions
    panel.Children().Append(CreateSectionHeader(L"Custom Dimensions"));

    auto heightCtrl = CreateSliderSettingCard(
        L"Taskbar Height",
        L"Height of the taskbar in logical pixels (Windows default: 48 px, compact: 32 px, ultra: 24 px).",
        plugin_name, "height", 20.0, 128.0, 1.0, 48.0, L"px"
    );
    panel.Children().Append(heightCtrl.card);

    auto padTopCtrl = CreateSliderSettingCard(
        L"Top Padding Offset",
        L"Fine-tune vertical icon alignment from top in logical pixels (default: 0 px).",
        plugin_name, "padding_top", -20.0, 20.0, 1.0, 0.0, L"px"
    );
    panel.Children().Append(padTopCtrl.card);

    auto padBotCtrl = CreateSliderSettingCard(
        L"Bottom Padding Offset",
        L"Fine-tune vertical icon alignment from bottom in logical pixels (default: 0 px).",
        plugin_name, "padding_bottom", -20.0, 20.0, 1.0, 0.0, L"px"
    );
    panel.Children().Append(padBotCtrl.card);

    auto spacingCtrl = CreateSliderSettingCard(
        L"Icon Spacing",
        L"Horizontal spacing between taskbar icons in logical pixels (default: 0 px standard, 4 px relaxed).",
        plugin_name, "icon_spacing", 0.0, 32.0, 1.0, 0.0, L"px"
    );
    panel.Children().Append(spacingCtrl.card);

    // Action Bar
    auto actionBar = CreateActionBar(L"Apply Dimensions");
    panel.Children().Append(actionBar.panel);

    // Helper to apply geometry across controls and disk
    auto applyGeometry = [plugin_name, heightCtrl, padTopCtrl, padBotCtrl, spacingCtrl, actionBar, masterToggle](int h, int pt, int pb, int sp, const std::wstring& msg) {
        if (heightCtrl.set_value) heightCtrl.set_value(h); else heightCtrl.slider->Value(h);
        if (padTopCtrl.set_value) padTopCtrl.set_value(pt); else padTopCtrl.slider->Value(pt);
        if (padBotCtrl.set_value) padBotCtrl.set_value(pb); else padBotCtrl.slider->Value(pb);
        if (spacingCtrl.set_value) spacingCtrl.set_value(sp); else spacingCtrl.slider->Value(sp);

        if (masterToggle && !masterToggle->IsOn()) {
            masterToggle->IsOn(true);
        }

        std::vector<SettingKeyValue> items;
        items.push_back({ "enabled", cJSON_CreateBool(true) });
        items.push_back({ "height", cJSON_CreateNumber(h) });
        items.push_back({ "padding_top", cJSON_CreateNumber(pt) });
        items.push_back({ "padding_bottom", cJSON_CreateNumber(pb) });
        items.push_back({ "icon_spacing", cJSON_CreateNumber(sp) });

        SaveMultipleAndReload(plugin_name, items);
        GuiIpcEnablePlugin(plugin_name);
        ShowTimedStatus(actionBar.statusLbl, msg);
    };

    defaultTaskbarBtn.Click([plugin_name, heightCtrl, padTopCtrl, padBotCtrl, spacingCtrl, actionBar, masterToggle](IInspectable const&, RoutedEventArgs const&) {
        if (heightCtrl.set_value) heightCtrl.set_value(48); else heightCtrl.slider->Value(48);
        if (padTopCtrl.set_value) padTopCtrl.set_value(0); else padTopCtrl.slider->Value(0);
        if (padBotCtrl.set_value) padBotCtrl.set_value(0); else padBotCtrl.slider->Value(0);
        if (spacingCtrl.set_value) spacingCtrl.set_value(0); else spacingCtrl.slider->Value(0);

        if (masterToggle) {
            masterToggle->IsOn(false);
        }

        std::vector<SettingKeyValue> items;
        items.push_back({ "enabled", cJSON_CreateBool(false) });
        items.push_back({ "height", cJSON_CreateNumber(48) });
        items.push_back({ "padding_top", cJSON_CreateNumber(0) });
        items.push_back({ "padding_bottom", cJSON_CreateNumber(0) });
        items.push_back({ "icon_spacing", cJSON_CreateNumber(0) });

        SaveMultipleAndReload(plugin_name, items);
        GuiIpcDisablePlugin(plugin_name);
        ShowTimedStatus(actionBar.statusLbl, L"✓ Taskbar restored to Windows default factory position. Subsystem disabled.");
    });

    presetDefault.Click([plugin_name, heightCtrl, padTopCtrl, padBotCtrl, spacingCtrl, actionBar, masterToggle](IInspectable const&, RoutedEventArgs const&) {
        if (heightCtrl.set_value) heightCtrl.set_value(48); else heightCtrl.slider->Value(48);
        if (padTopCtrl.set_value) padTopCtrl.set_value(0); else padTopCtrl.slider->Value(0);
        if (padBotCtrl.set_value) padBotCtrl.set_value(0); else padBotCtrl.slider->Value(0);
        if (spacingCtrl.set_value) spacingCtrl.set_value(0); else spacingCtrl.slider->Value(0);

        if (masterToggle) {
            masterToggle->IsOn(false);
        }

        std::vector<SettingKeyValue> items;
        items.push_back({ "enabled", cJSON_CreateBool(false) });
        items.push_back({ "height", cJSON_CreateNumber(48) });
        items.push_back({ "padding_top", cJSON_CreateNumber(0) });
        items.push_back({ "padding_bottom", cJSON_CreateNumber(0) });
        items.push_back({ "icon_spacing", cJSON_CreateNumber(0) });

        SaveMultipleAndReload(plugin_name, items);
        GuiIpcDisablePlugin(plugin_name);
        ShowTimedStatus(actionBar.statusLbl, L"✓ Restored to Windows default taskbar (48 px). Subsystem disabled.");
    });

    presetCompact.Click([applyGeometry](IInspectable const&, RoutedEventArgs const&) {
        applyGeometry(32, 0, 0, 4, L"✓ Applied Compact preset (32 px).");
    });

    presetUltra.Click([applyGeometry](IInspectable const&, RoutedEventArgs const&) {
        applyGeometry(24, 0, 0, 4, L"✓ Applied Ultra-Compact preset (24 px).");
    });

    actionBar.applyBtn.Click([plugin_name, heightCtrl, padTopCtrl, padBotCtrl, spacingCtrl, actionBar, masterToggle](IInspectable const&, RoutedEventArgs const&) {
        int h = (int)std::round(heightCtrl.slider->Value());
        int pt = (int)std::round(padTopCtrl.slider->Value());
        int pb = (int)std::round(padBotCtrl.slider->Value());
        int sp = (int)std::round(spacingCtrl.slider->Value());

        if (masterToggle && !masterToggle->IsOn()) {
            masterToggle->IsOn(true);
        }

        std::vector<SettingKeyValue> items;
        items.push_back({ "enabled", cJSON_CreateBool(true) });
        items.push_back({ "height", cJSON_CreateNumber(h) });
        items.push_back({ "padding_top", cJSON_CreateNumber(pt) });
        items.push_back({ "padding_bottom", cJSON_CreateNumber(pb) });
        items.push_back({ "icon_spacing", cJSON_CreateNumber(sp) });

        SaveMultipleAndReload(plugin_name, items);
        GuiIpcEnablePlugin(plugin_name);
        ShowTimedStatus(actionBar.statusLbl, L"✓ Dimensions saved to default_config.jsonc and applied!");
    });

    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

// ============================================================================
// 2. Icon Magnification Page
// ============================================================================

Page CreateIconMagnificationPage()
{
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(8);
    panel.Padding(Thickness{ 28, 24, 28, 28 });

    panel.Children().Append(CreatePageHeader(
        L"Icon Magnification & Dock",
        L"Configure macOS Dock-style magnification scale, physics influence radius, duration, and falloff curves."
    ));

    const std::string plugin_name = "icon_hover";

    // Master Toggle
    auto masterToggle = std::make_shared<ToggleSwitch>();
    panel.Children().Append(CreateToggleSettingCard(
        L"Enable Icon Magnification",
        L"Master toggle for the DirectComposition hardware-accelerated icon magnification layer.",
        plugin_name, "enabled", true, masterToggle
    ));

    panel.Children().Append(CreateSectionHeader(L"Magnification Geometry"));

    auto scaleCtrl = CreateSliderSettingCard(
        L"Max Magnification Scale",
        L"Peak scale multiplier when cursor is directly over an icon (1.0x to 10.0x).",
        plugin_name, "max_scale", 1.0, 10.0, 0.05, 6.5, L"x", 2
    );
    panel.Children().Append(scaleCtrl.card);

    auto radiusCtrl = CreateSliderSettingCard(
        L"Effect Influence Radius",
        L"Distance in pixels over which neighboring icons are smoothly scaled (50 to 500 px).",
        plugin_name, "radius", 50.0, 500.0, 5.0, 190.0, L"px"
    );
    panel.Children().Append(radiusCtrl.card);

    auto speedCtrl = CreateSliderSettingCard(
        L"Animation Duration",
        L"Duration in milliseconds for the settle spring animation when cursor leaves (50 to 500 ms).",
        plugin_name, "speed_ms", 50.0, 500.0, 10.0, 500.0, L"ms"
    );
    panel.Children().Append(speedCtrl.card);

    panel.Children().Append(CreateSectionHeader(L"Curve & Layering"));

    // Curve ComboBox Card
    ComboBox curveCombo;
    curveCombo.Width(180);
    curveCombo.Items().Append(box_value(L"Gaussian (Smooth)"));
    curveCombo.Items().Append(box_value(L"Cubic (Snappy)"));
    curveCombo.Items().Append(box_value(L"Cosine (Gentle)"));
    curveCombo.Items().Append(box_value(L"Linear (Uniform)"));

    std::string cur_curve = GetStringSetting(plugin_name, "curve", "linear");
    if (cur_curve == "gaussian") curveCombo.SelectedIndex(0);
    else if (cur_curve == "cubic") curveCombo.SelectedIndex(1);
    else if (cur_curve == "cosine") curveCombo.SelectedIndex(2);
    else if (cur_curve == "linear") curveCombo.SelectedIndex(3);
    else curveCombo.SelectedIndex(3);

    curveCombo.SelectionChanged([plugin_name](IInspectable const& sender, SelectionChangedEventArgs const&) {
        int idx = sender.as<ComboBox>().SelectedIndex();
        const char* curves[] = { "gaussian", "cubic", "cosine", "linear" };
        if (idx >= 0 && idx < 4) {
            SaveAndReload(plugin_name, "curve", cJSON_CreateString(curves[idx]));
        }
    });

    panel.Children().Append(CreateSettingsCard(
        L"Easing Falloff Curve",
        L"Mathematical distribution profile applied across neighboring icons.",
        curveCombo
    ));

    panel.Children().Append(CreateToggleSettingCard(
        L"Keep Overlay On Top (Z-Order Protection)",
        L"Continuously enforces HWND_TOPMOST so taskbar clicks do not obscure magnification animations.",
        plugin_name, "keep_on_top", true
    ));

    auto actionBar = CreateActionBar(L"Apply Magnification Settings");
    panel.Children().Append(actionBar.panel);

    actionBar.applyBtn.Click([plugin_name, scaleCtrl, radiusCtrl, speedCtrl, curveCombo, actionBar](IInspectable const&, RoutedEventArgs const&) {
        double scale = scaleCtrl.slider->Value();
        int rad = (int)std::round(radiusCtrl.slider->Value());
        int spd = (int)std::round(speedCtrl.slider->Value());
        int c_idx = curveCombo.SelectedIndex();
        const char* curves[] = { "gaussian", "cubic", "cosine", "linear" };
        const char* chosen_curve = (c_idx >= 0 && c_idx < 4) ? curves[c_idx] : "linear";

        std::vector<SettingKeyValue> items;
        items.push_back({ "max_scale", cJSON_CreateNumber(scale) });
        items.push_back({ "radius", cJSON_CreateNumber(rad) });
        items.push_back({ "speed_ms", cJSON_CreateNumber(spd) });
        items.push_back({ "curve", cJSON_CreateString(chosen_curve) });

        SaveMultipleAndReload(plugin_name, items);
        ShowTimedStatus(actionBar.statusLbl, L"✓ Magnification settings saved and applied!");
    });

    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

// ============================================================================
// 3. Physics & Interaction Effects Page
// ============================================================================

Page CreatePhysicsSettingsPage()
{
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(8);
    panel.Padding(Thickness{ 28, 24, 28, 28 });

    panel.Children().Append(CreatePageHeader(
        L"Physics & Interaction Effects",
        L"Tune notification spring dynamics, 3D perspective pitch/yaw tilt, and drag-and-drop expansion mechanics."
    ));

    const std::string plugin_name = "icon_hover";

    // SECTION 1: Notification Spring Bouncing
    panel.Children().Append(CreateSectionHeader(L"Notification Inertial Bouncing"));

    panel.Children().Append(CreateToggleSettingCard(
        L"Enable Notification Bounce",
        L"Taskbar icons perform an upward elastic spring bounce when receiving background notifications or alerts.",
        plugin_name, "bounce_enabled", true
    ));

    auto bounceStrengthCtrl = CreateSliderSettingCard(
        L"Bounce Impulse Strength",
        L"Initial upward launch velocity applied when an app receives a notification (200 to 1500 px/s).",
        plugin_name, "bounce_strength", 200.0, 1500.0, 25.0, 800.0, L"px/s"
    );
    panel.Children().Append(bounceStrengthCtrl.card);

    Button testBounceBtn;
    testBounceBtn.Content(box_value(L"Test Notification Bounce"));
    testBounceBtn.Padding(Thickness{ 16, 6, 16, 6 });
    testBounceBtn.VerticalAlignment(VerticalAlignment::Center);

    TextBlock testStatus;
    testStatus.VerticalAlignment(VerticalAlignment::Center);
    testStatus.FontSize(12.0);
    testStatus.Opacity(0.8);

    StackPanel testRow;
    testRow.Orientation(Orientation::Horizontal);
    testRow.Spacing(12);
    testRow.VerticalAlignment(VerticalAlignment::Center);
    testRow.Children().Append(testBounceBtn);
    testRow.Children().Append(testStatus);

    testBounceBtn.Click([testStatus](IInspectable const&, RoutedEventArgs const&) {
        testStatus.Text(L"Switch window or click away in 2s to see bounce...");
        auto timer = std::make_shared<DispatcherTimer>();
        timer->Interval(std::chrono::milliseconds(2000));
        timer->Tick([timer, testStatus](IInspectable const&, IInspectable const&) {
            try {
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
            } catch (...) {}
        });
        timer->Start();
    });

    panel.Children().Append(CreateSettingsCard(
        L"Test Bounce Simulation",
        L"Triggers a simulated notification flash after a 2-second delay to preview the elastic physics.",
        testRow
    ));

    // SECTION 2: 3D Tilt & Perspective
    panel.Children().Append(CreateSectionHeader(L"3D Tilt & Perspective"));

    panel.Children().Append(CreateToggleSettingCard(
        L"Enable 3D Tilt Perspective",
        L"DirectComposition 3D matrix transformation tilting icons toward cursor coordinates in real time.",
        plugin_name, "tilt_enabled", true
    ));

    auto tiltAngleCtrl = CreateSliderSettingCard(
        L"Maximum Tilt Angle",
        L"Maximum pitch and yaw rotation angle in degrees when cursor is near icon boundaries (0° to 45°).",
        plugin_name, "max_tilt_angle", 0.0, 45.0, 1.0, 20.0, L"°"
    );
    panel.Children().Append(tiltAngleCtrl.card);

    // SECTION 3: Drag & Drop Physics
    panel.Children().Append(CreateSectionHeader(L"Drag-and-Drop Physics"));

    panel.Children().Append(CreateToggleSettingCard(
        L"Enable Drag & Drop Physics",
        L"During mouse drag operations, scales down the held icon and spreads adjacent icons apart horizontally.",
        plugin_name, "drag_drop_enabled", true
    ));

    auto dragRecessionCtrl = CreateSliderSettingCard(
        L"Drag Recession Scale",
        L"Target scale multiplier applied to an icon while being clicked and dragged (0.50x to 1.00x).",
        plugin_name, "drag_recession_scale", 0.50, 1.00, 0.05, 0.65, L"x", 2
    );
    panel.Children().Append(dragRecessionCtrl.card);

    auto dropPushCtrl = CreateSliderSettingCard(
        L"Drop Zone Push Distance",
        L"Horizontal parting distance in pixels for neighbor icons during drag (10 to 120 px).",
        plugin_name, "drop_zone_push", 10.0, 120.0, 5.0, 50.0, L"px"
    );
    panel.Children().Append(dropPushCtrl.card);

    auto actionBar = CreateActionBar(L"Apply Physics Settings");
    panel.Children().Append(actionBar.panel);

    actionBar.applyBtn.Click([plugin_name, bounceStrengthCtrl, tiltAngleCtrl, dragRecessionCtrl, dropPushCtrl, actionBar](IInspectable const&, RoutedEventArgs const&) {
        std::vector<SettingKeyValue> items;
        items.push_back({ "bounce_strength", cJSON_CreateNumber(bounceStrengthCtrl.slider->Value()) });
        items.push_back({ "max_tilt_angle", cJSON_CreateNumber(std::round(tiltAngleCtrl.slider->Value())) });
        items.push_back({ "drag_recession_scale", cJSON_CreateNumber(dragRecessionCtrl.slider->Value()) });
        items.push_back({ "drop_zone_push", cJSON_CreateNumber(std::round(dropPushCtrl.slider->Value())) });

        SaveMultipleAndReload(plugin_name, items);
        ShowTimedStatus(actionBar.statusLbl, L"✓ Physics settings saved and applied!");
    });

    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

// ============================================================================
// 4. Start Button Customization Page
// ============================================================================

Page CreateStartButtonPage()
{
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(8);
    panel.Padding(Thickness{ 28, 24, 28, 28 });

    panel.Children().Append(CreatePageHeader(
        L"Start Button Customization",
        L"Replace the Windows 11 Start button with a custom image while preserving hover magnification and click forwarding."
    ));

    const std::string plugin_name = "icon_hover";

    panel.Children().Append(CreateSectionHeader(L"Custom Image File"));

    auto start_image_box = std::make_shared<TextBox>();
    std::string cur_img = GetStringSetting(plugin_name, "start_image_path", "");
    start_image_box->Text(to_hstring(cur_img));
    start_image_box->PlaceholderText(L"e.g. Config/start_button.png or start_button.svg");
    start_image_box->MinWidth(320);
    start_image_box->VerticalAlignment(VerticalAlignment::Center);

    start_image_box->LostFocus([plugin_name](IInspectable const& sender, RoutedEventArgs const&) {
        std::string val = to_string(sender.as<TextBox>().Text());
        SaveAndReload(plugin_name, "start_image_path", cJSON_CreateString(val.c_str()));
    });

    Button browseBtn;
    browseBtn.Content(box_value(L"Browse..."));
    browseBtn.Padding(Thickness{ 16, 6, 16, 6 });
    browseBtn.VerticalAlignment(VerticalAlignment::Center);

    Button resetBtn;
    resetBtn.Content(box_value(L"Restore Windows Default"));
    resetBtn.Padding(Thickness{ 14, 6, 14, 6 });
    resetBtn.VerticalAlignment(VerticalAlignment::Center);

    Button clearBtn;
    clearBtn.Content(box_value(L"Disable"));
    clearBtn.Padding(Thickness{ 14, 6, 14, 6 });
    clearBtn.VerticalAlignment(VerticalAlignment::Center);

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
        box_ptr->Text(L"");
        SaveAndReload(plugin_name, "start_image_path", cJSON_CreateString(""));
    });

    clearBtn.Click([plugin_name, box_ptr](IInspectable const&, RoutedEventArgs const&) {
        box_ptr->Text(L"");
        SaveAndReload(plugin_name, "start_image_path", cJSON_CreateString(""));
    });

    StackPanel inputRow;
    inputRow.Orientation(Orientation::Horizontal);
    inputRow.Spacing(10);
    inputRow.VerticalAlignment(VerticalAlignment::Center);
    inputRow.Children().Append(*start_image_box);
    inputRow.Children().Append(browseBtn);
    inputRow.Children().Append(resetBtn);
    inputRow.Children().Append(clearBtn);

    panel.Children().Append(CreateSettingsCard(
        L"Start Button Image Path",
        L"Specify a custom image file (PNG, SVG, JPG, BMP, ICO). Click 'Disable' to restore the standard Windows 11 icon.",
        inputRow
    ));

    TextBlock infoBlock;
    infoBlock.Text(L"The custom start button image participates in hover magnification, spring physics, and forwards shell clicks to the interactive Start menu in explorer.exe.");
    infoBlock.FontSize(12.0);
    infoBlock.Opacity(0.72);
    infoBlock.TextWrapping(TextWrapping::Wrap);

    Border infoCard;
    infoCard.CornerRadius(CornerRadius{ 4, 4, 4, 4 });
    infoCard.BorderThickness(Thickness{ 1, 1, 1, 1 });
    infoCard.Margin(Thickness{ 0, 0, 0, 8 });
    infoCard.Padding(Thickness{ 16, 12, 16, 12 });
    try {
        auto res = Application::Current().Resources();
        if (res.HasKey(box_value(L"CardBackgroundFillColorDefaultBrush"))) {
            infoCard.Background(res.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>());
        }
        if (res.HasKey(box_value(L"CardStrokeColorDefaultBrush"))) {
            infoCard.BorderBrush(res.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>());
        }
    } catch (...) {}
    infoCard.Child(infoBlock);
    panel.Children().Append(infoCard);

    auto actionBar = CreateActionBar(L"Apply Start Button");
    panel.Children().Append(actionBar.panel);

    actionBar.applyBtn.Click([plugin_name, box_ptr, actionBar](IInspectable const&, RoutedEventArgs const&) {
        std::string img_path = to_string(box_ptr->Text());
        SaveAndReload(plugin_name, "start_image_path", cJSON_CreateString(img_path.c_str()));
        ShowTimedStatus(actionBar.statusLbl, L"✓ Start button image saved and reloaded!");
    });

    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

// ============================================================================
// 5. Dynamic Island (Media Pill) Page
// ============================================================================

Page CreateDynamicIslandPage()
{
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(8);
    panel.Padding(Thickness{ 28, 24, 28, 28 });

    panel.Children().Append(CreatePageHeader(
        L"Dynamic Island (Media Pill)",
        L"Hardware-accelerated frosted acrylic capsule anchored adjacent to the system tray displaying live Windows media status."
    ));

    Border guideCard;
    guideCard.CornerRadius(CornerRadius{ 6, 6, 6, 6 });
    guideCard.BorderThickness(Thickness{ 1, 1, 1, 1 });
    guideCard.Margin(Thickness{ 0, 0, 0, 12 });
    guideCard.Padding(Thickness{ 16, 14, 16, 14 });
    try {
        auto res = Application::Current().Resources();
        if (res.HasKey(box_value(L"CardBackgroundFillColorDefaultBrush"))) {
            guideCard.Background(res.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>());
        }
        if (res.HasKey(box_value(L"CardStrokeColorDefaultBrush"))) {
            guideCard.BorderBrush(res.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>());
        }
    } catch (...) {}

    StackPanel guideStack;
    guideStack.Spacing(4);

    TextBlock guideTitle;
    guideTitle.Text(L"💡 How Dynamic Island Works on the Windows Taskbar");
    guideTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    guideTitle.FontSize(13.5);
    guideStack.Children().Append(guideTitle);

    TextBlock guideBody;
    guideBody.Text(L"• Dynamic Island is an active media player capsule (Spotify, YouTube in Chrome/Edge, Apple Music, VLC).\n• Idle Invariant: When NO media is playing, the pill stays invisible (0% opacity) to keep the taskbar clean.\n• When media plays: The capsule illuminates beside the system tray. Hovering expands track details; clicking pauses/plays.");
    guideBody.FontSize(12.0);
    guideBody.Opacity(0.85);
    guideBody.TextWrapping(TextWrapping::Wrap);
    guideStack.Children().Append(guideBody);

    guideCard.Child(guideStack);
    panel.Children().Append(guideCard);

    ToggleSwitch diToggle;
    diToggle.OffContent(box_value(L"Off"));
    diToggle.OnContent(box_value(L"On"));
    diToggle.IsOn(GetDynamicIslandBool("enabled", true));
    diToggle.VerticalAlignment(VerticalAlignment::Center);
    diToggle.Toggled([](IInspectable const& sender, RoutedEventArgs const&) {
        SaveDynamicIslandSetting("enabled", cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
    });

    panel.Children().Append(CreateSettingsCard(
        L"Enable Dynamic Island",
        L"Master toggle for the frosted acrylic media pill anchored to the taskbar notification tray.",
        diToggle
    ));

    ToggleSwitch idlePillToggle;
    idlePillToggle.OffContent(box_value(L"Hover Reveal"));
    idlePillToggle.OnContent(box_value(L"Always Visible"));
    idlePillToggle.IsOn(GetDynamicIslandBool("show_idle_pill", false));
    idlePillToggle.VerticalAlignment(VerticalAlignment::Center);
    idlePillToggle.Toggled([](IInspectable const& sender, RoutedEventArgs const&) {
        SaveDynamicIslandSetting("show_idle_pill", cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
    });

    panel.Children().Append(CreateSettingsCard(
        L"Idle Visibility Mode",
        L"Choose whether the compact capsule stays visible at resting opacity when media is idle, or hides and reveals only upon mouse hover proximity.",
        idlePillToggle
    ));

    panel.Children().Append(CreateSectionHeader(L"Capsule Dimensions & Layout"));

    auto padTrayCtrl = CreateDynamicIslandSliderSettingCard(
        L"Tray Margin Padding",
        L"Horizontal spacing in pixels between the system tray (TrayNotifyWnd) and the island (0 to 48 px).",
        "padding_tray", 0.0, 48.0, 1.0, 15.0, L"px"
    );
    panel.Children().Append(padTrayCtrl.card);

    auto compactCtrl = CreateDynamicIslandSliderSettingCard(
        L"Compact Resting Width",
        L"Base resting width in pixels showing compact playback status (40 to 160 px).",
        "compact_width", 40.0, 160.0, 2.0, 80.0, L"px"
    );
    panel.Children().Append(compactCtrl.card);

    auto expandedCtrl = CreateDynamicIslandSliderSettingCard(
        L"Expanded Announcement Width",
        L"Hovered and active track announcement width in pixels revealing media title & artist (160 to 400 px).",
        "expanded_width", 160.0, 400.0, 5.0, 240.0, L"px"
    );
    panel.Children().Append(expandedCtrl.card);

    auto heightCtrl = CreateDynamicIslandSliderSettingCard(
        L"Capsule Height",
        L"Vertical height in pixels of the pill visual (20 to 48 px). Centered vertically within the taskbar.",
        "height", 20.0, 48.0, 1.0, 30.0, L"px"
    );
    panel.Children().Append(heightCtrl.card);

    auto actionBar = CreateActionBar(L"Apply Island Dimensions");
    panel.Children().Append(actionBar.panel);

    actionBar.applyBtn.Click([padTrayCtrl, compactCtrl, expandedCtrl, heightCtrl, diToggle, idlePillToggle, actionBar](IInspectable const&, RoutedEventArgs const&) {
        std::vector<SettingKeyValue> items;
        items.push_back({ "enabled", cJSON_CreateBool(diToggle.IsOn()) });
        items.push_back({ "show_idle_pill", cJSON_CreateBool(idlePillToggle.IsOn()) });
        items.push_back({ "padding_tray", cJSON_CreateNumber(std::round(padTrayCtrl.slider->Value())) });
        items.push_back({ "compact_width", cJSON_CreateNumber(std::round(compactCtrl.slider->Value())) });
        items.push_back({ "expanded_width", cJSON_CreateNumber(std::round(expandedCtrl.slider->Value())) });
        items.push_back({ "height", cJSON_CreateNumber(std::round(heightCtrl.slider->Value())) });

        SaveDynamicIslandMultiple(items);
        ShowTimedStatus(actionBar.statusLbl, L"✓ Dynamic Island settings saved and applied!");
    });

    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

// ============================================================================
// 6. Generic Schema-Driven Settings Page (for 3rd-party plugins)
// ============================================================================

static Page CreateGenericPluginSettingsPage(const std::string& plugin_name, const std::string& schema_json)
{
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

// ============================================================================
// Router: CreateSettingsPage
// ============================================================================

Page CreateSettingsPage(const std::string& plugin_name, const std::string& schema_json)
{
    if (plugin_name == "taskbar_resize") {
        return CreateTaskbarResizePage();
    }
    if (plugin_name == "icon_magnification" || plugin_name == "icon_hover") {
        return CreateIconMagnificationPage();
    }
    if (plugin_name == "icon_physics") {
        return CreatePhysicsSettingsPage();
    }
    if (plugin_name == "start_button") {
        return CreateStartButtonPage();
    }
    if (plugin_name == "dynamic_island") {
        return CreateDynamicIslandPage();
    }

    return CreateGenericPluginSettingsPage(plugin_name, schema_json);
}

}
