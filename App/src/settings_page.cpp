#include "settings_page.h"
#include "config_io.h"
#include "gui_ipc_client.h"
#include <cJSON.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Text.h>
#include <cmath>
#include <vector>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::TaskbarEngine {

static void SaveAndReload(const std::string& plugin_name, const std::string& key, cJSON* value)
{
    std::wstring path = ConfigIO_GetConfigPath();
    cJSON* root = ConfigIO_Load(path);
    if (!root) {
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
    title.Style(Application::Current().Resources().Lookup(box_value(L"TitleTextBlockStyle")).as<Style>());
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
                    
                    if (cJSON* min = cJSON_GetObjectItem(setting, "min")) num.Minimum(min->valuedouble);
                    if (cJSON* max = cJSON_GetObjectItem(setting, "max")) num.Maximum(max->valuedouble);
                    if (cJSON* step = cJSON_GetObjectItem(setting, "step")) num.SmallChange(step->valuedouble);
                    
                    double val = 0;
                    if (current && cJSON_IsNumber(current)) val = current->valuedouble;
                    else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) val = def->valuedouble;
                    
                    num.Value(val);
                    num.ValueChanged([plugin_name, key, type](NumberBox const& sender, NumberBoxValueChangedEventArgs const& args) {
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
