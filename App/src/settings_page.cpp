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

#include <mutex>

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

Page CreateSettingsPage(const std::string& plugin_name, const std::string& schema_json)
{
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(16);
    panel.Padding(Thickness{ 24, 24, 24, 24 });
    
    TextBlock title;
    title.Text(to_hstring(plugin_name + " Settings"));
    // title.Style(Application::Current().Resources().Lookup(box_value(L"TitleTextBlockStyle")).as<Style>());
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
                } else if (type == "int" || type == "float") {
                    NumberBox num;
                    num.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
                    
                    if (cJSON* min = cJSON_GetObjectItem(setting, "min")) {
                        if (cJSON_IsNumber(min)) num.Minimum(min->valuedouble);
                    }
                    if (cJSON* max = cJSON_GetObjectItem(setting, "max")) {
                        if (cJSON_IsNumber(max)) num.Maximum(max->valuedouble);
                    }
                    if (cJSON* step = cJSON_GetObjectItem(setting, "step")) {
                        if (cJSON_IsNumber(step)) num.SmallChange(step->valuedouble);
                    }
                    
                    double val = 0;
                    if (current && cJSON_IsNumber(current)) val = current->valuedouble;
                    else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) val = def->valuedouble;
                    
                    num.Value(val);
                    num.ValueChanged([plugin_name, key, type](NumberBox const&, NumberBoxValueChangedEventArgs const& args) {
                        if (std::isnan(args.NewValue())) return;
                        if (type == "int") {
                            SaveAndReload(plugin_name, key, cJSON_CreateNumber(std::round(args.NewValue())));
                        } else {
                            SaveAndReload(plugin_name, key, cJSON_CreateNumber(args.NewValue()));
                        }
                    });
                    itemPanel.Children().Append(num);
                } else if (type == "enum") {
                    ComboBox combo;
                    cJSON* options = cJSON_GetObjectItem(setting, "options");
                    
                    std::string val_str = "";
                    if (current && cJSON_IsString(current)) val_str = current->valuestring;
                    else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) val_str = def->valuestring;
                    
                    int selected_idx = 0;
                    int i = 0;
                    
                    // We need a shared pointer for the options to keep it alive for the lambda, 
                    // or just capture the strings. It's safer to capture the strings.
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
