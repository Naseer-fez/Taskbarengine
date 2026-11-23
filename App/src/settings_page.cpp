#include "settings_page.h"
#include "config_io.h"
#include "gui_ipc_client.h"
#include <cJSON.h>
#include <sdk/te_widget.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#undef FindText
#undef FindTextW
#undef FindTextA
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <vector>

#include <memory>
#include <functional>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <commdlg.h>
#include <fstream>
#include <sstream>
#include <unordered_map>

#pragma comment(lib, "comdlg32.lib")

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace winrt::TaskbarEngine {

static std::mutex g_settings_mutex;

struct WidgetCatalogItem { std::string id; std::string name; std::string settings_json; };

static std::vector<WidgetCatalogItem> ReadWidgetCatalog()
{
    std::vector<WidgetCatalogItem> items;
    wchar_t exe_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return items;
    std::wstring base(exe_path);
    const size_t slash = base.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return items;
    base.resize(slash);
    if (GetFileAttributesW((base + L"\\Widgets").c_str()) == INVALID_FILE_ATTRIBUTES) {
        const size_t parent = base.find_last_of(L"\\/");
        if (parent != std::wstring::npos) base.resize(parent);
    }
    const std::wstring search = base + L"\\Widgets\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(search.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return items;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == L'.') continue;
        std::wstring manifest_path = base + L"\\Widgets\\" + data.cFileName + L"\\widget.json";
        std::ifstream input(manifest_path, std::ios::binary);
        if (!input) continue;
        std::ostringstream contents;
        contents << input.rdbuf();
        cJSON* manifest = cJSON_Parse(contents.str().c_str());
        if (!manifest) continue;
        cJSON* id = cJSON_GetObjectItemCaseSensitive(manifest, "id");
        cJSON* name = cJSON_GetObjectItemCaseSensitive(manifest, "name");
        cJSON* module = cJSON_GetObjectItemCaseSensitive(manifest, "module");
        cJSON* api_version = cJSON_GetObjectItemCaseSensitive(manifest, "api_version");
        cJSON* manifest_version = cJSON_GetObjectItemCaseSensitive(manifest, "manifest_version");
        cJSON* settings = cJSON_GetObjectItemCaseSensitive(manifest, "settings");
        if (cJSON_IsString(id) && id->valuestring && cJSON_IsString(name) && name->valuestring) {
            const std::string stable_id = id->valuestring;
            const bool valid = !stable_id.empty() && stable_id.size() <= 64 &&
                std::all_of(stable_id.begin(), stable_id.end(), [](unsigned char ch) {
                    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
                });
            const bool valid_module = cJSON_IsString(module) && module->valuestring && module->valuestring[0] &&
                std::string(module->valuestring).find_first_of("\\/:*") == std::string::npos &&
                std::string(module->valuestring).find("..") == std::string::npos;
            const int directory_chars = WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, nullptr, 0, nullptr, nullptr);
            std::string folder(directory_chars > 1 ? static_cast<size_t>(directory_chars) : 0, '\0');
            if (directory_chars > 1) {
                WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, folder.data(), directory_chars, nullptr, nullptr);
                folder.pop_back();
            }
            if (valid && valid_module && stable_id == folder && cJSON_IsNumber(api_version) && api_version->valueint == TE_WIDGET_API_VERSION &&
                cJSON_IsNumber(manifest_version) && manifest_version->valueint == 1 &&
                std::none_of(items.begin(), items.end(), [&](const auto& item) { return item.id == stable_id; })) {
                std::string settings_json = "[]";
                if (cJSON_IsArray(settings)) {
                    char* printed = cJSON_PrintUnformatted(settings);
                    if (printed) { settings_json = printed; cJSON_free(printed); }
                }
                items.push_back({ stable_id, name->valuestring, settings_json });
            }
        }
        cJSON_Delete(manifest);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return items;
}

static std::string MergeWidgetCatalogSchema(const std::string& base_schema,
                                            const std::vector<WidgetCatalogItem>& catalog)
{
    cJSON* schema = cJSON_Parse(base_schema.c_str());
    if (!schema || !cJSON_IsObject(schema)) {
        if (schema) cJSON_Delete(schema);
        schema = cJSON_CreateObject();
    }
    if (!schema) return base_schema;
    cJSON* settings = cJSON_GetObjectItemCaseSensitive(schema, "settings");
    if (!cJSON_IsArray(settings)) {
        settings = cJSON_CreateArray();
        if (!settings || !cJSON_AddItemToObject(schema, "settings", settings)) {
            if (settings) cJSON_Delete(settings);
            cJSON_Delete(schema);
            return base_schema;
        }
    }
    if (catalog.size() || base_schema == "{}") {
        static const char* host_settings = R"JSON([
            {"key":"enabled","label":"Enable widget bar","type":"bool","default":true},
            {"key":"layout","label":"Widget order","type":"string","default":"clock,sysmon,media"},
            {"key":"position_percent","label":"Position along taskbar","type":"slider","default":100,"min":0,"max":100,"step":1,"unit":"%"},
            {"key":"gap","label":"Widget spacing","type":"slider","default":10,"min":4,"max":32,"step":1,"unit":"DIP"}
        ])JSON";
        cJSON* host = cJSON_Parse(host_settings);
        if (cJSON_IsArray(host)) {
            cJSON* descriptor = nullptr;
            cJSON_ArrayForEach(descriptor, host) {
                cJSON* key = cJSON_GetObjectItemCaseSensitive(descriptor, "key");
                bool duplicate = false;
                cJSON* existing = nullptr;
                cJSON_ArrayForEach(existing, settings) {
                    cJSON* existing_key = cJSON_GetObjectItemCaseSensitive(existing, "key");
                    if (cJSON_IsString(existing_key) && cJSON_IsString(key) &&
                        strcmp(existing_key->valuestring, key->valuestring) == 0) { duplicate = true; break; }
                }
                if (!duplicate) {
                    cJSON* copy = cJSON_Duplicate(descriptor, 1);
                    if (copy && !cJSON_AddItemToArray(settings, copy)) cJSON_Delete(copy);
                }
            }
        }
        if (host) cJSON_Delete(host);
    }
    for (const auto& widget : catalog) {
        cJSON* additions = cJSON_Parse(widget.settings_json.c_str());
        if (!cJSON_IsArray(additions)) { if (additions) cJSON_Delete(additions); continue; }
        cJSON* descriptor = nullptr;
        cJSON_ArrayForEach(descriptor, additions) {
            cJSON* key = cJSON_GetObjectItemCaseSensitive(descriptor, "key");
            if (!cJSON_IsString(key) || !key->valuestring || !key->valuestring[0]) continue;
            const std::string full_key = widget.id + "." + key->valuestring;
            bool duplicate = false;
            cJSON* existing = nullptr;
            cJSON_ArrayForEach(existing, settings) {
                cJSON* existing_key = cJSON_GetObjectItemCaseSensitive(existing, "key");
                if (cJSON_IsString(existing_key) && existing_key->valuestring && full_key == existing_key->valuestring) { duplicate = true; break; }
            }
            if (duplicate) continue;
            cJSON* copy = cJSON_Duplicate(descriptor, 1);
            if (!copy) continue;
            cJSON* copied_key = cJSON_CreateString(full_key.c_str());
            if (!copied_key) { cJSON_Delete(copy); continue; }
            if (!cJSON_ReplaceItemInObjectCaseSensitive(copy, "key", copied_key)) {
                cJSON_Delete(copied_key);
                cJSON_Delete(copy);
                continue;
            }
            if (!cJSON_AddItemToArray(settings, copy)) cJSON_Delete(copy);
        }
        cJSON_Delete(additions);
    }
    char* serialized = cJSON_PrintUnformatted(schema);
    const std::string result = serialized ? serialized : base_schema;
    if (serialized) cJSON_free(serialized);
    cJSON_Delete(schema);
    return result;
}

static void QueueSettingsWork(std::function<void()> work)
{
    struct WorkerState {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<std::function<void()>> pending;
    };
    // Intentionally process-lifetime storage: a settings write may still be in flight
    // while WinUI is tearing down, and must not race static destruction.
    static WorkerState* state = [] {
        auto* worker = new WorkerState();
        std::thread([worker] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(worker->mutex);
                    worker->ready.wait(lock, [worker] { return !worker->pending.empty(); });
                    task = std::move(worker->pending.front());
                    worker->pending.pop_front();
                }
                try { task(); } catch (...) { /* Keep the settings worker alive. */ }
            }
        }).detach();
        return worker;
    }();

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->pending.emplace_back(std::move(work));
    }
    state->ready.notify_one();
}

static void SaveAndReloadNow(const std::string& plugin_name, const std::string& key, cJSON* value)
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
    
    const HRESULT set_hr = plugin_name == "widget_host" && key != "enabled"
        ? ConfigIO_SetWidgetValue(root, key.c_str(), value)
        : ConfigIO_SetPluginValue(root, plugin_name.c_str(), key.c_str(), value);
    if (SUCCEEDED(set_hr)) {
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
        cJSON* val = plugin_name == "widget_host" && key != "enabled"
            ? ConfigIO_GetWidgetValue(root, key.c_str())
            : ConfigIO_GetPluginValue(root, plugin_name.c_str(), key.c_str());
        if (!val && plugin_name == "widget_host" && key == "position_percent") {
            cJSON* widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
            cJSON* legacy = widgets ? cJSON_GetObjectItemCaseSensitive(widgets, "position") : nullptr;
            if (cJSON_IsString(legacy) && legacy->valuestring) {
                const double percent = strcmp(legacy->valuestring, "left") == 0 ? 0.0 :
                    strcmp(legacy->valuestring, "center") == 0 ? 50.0 : 100.0;
                res = cJSON_CreateNumber(percent);
            }
        }
        if (val && !res) {
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

static void SaveMultipleAndReloadNow(const std::string& plugin_name, std::vector<SettingKeyValue> items)
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
        if (plugin_name == "widget_host" && it.key != "enabled")
            ConfigIO_SetWidgetValue(root, it.key.c_str(), it.value);
        else
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

static void SaveDynamicIslandSettingNow(const std::string& key, cJSON* value)
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

static void SaveDynamicIslandMultipleNow(const std::vector<SettingKeyValue>& items)
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

// UI event handlers enqueue persistence and IPC work so slow storage or a
// stalled engine cannot stop WinUI's dispatcher from pumping input and paint.
static void SaveAndReload(const std::string& plugin_name, const std::string& key, cJSON* value)
{
    QueueSettingsWork([plugin_name, key, value] { SaveAndReloadNow(plugin_name, key, value); });
}

static void SaveMultipleAndReload(const std::string& plugin_name, std::vector<SettingKeyValue> items)
{
    QueueSettingsWork([plugin_name, items = std::move(items)]() mutable {
        SaveMultipleAndReloadNow(plugin_name, std::move(items));
    });
}

static void SaveDynamicIslandSetting(const std::string& key, cJSON* value)
{
    QueueSettingsWork([key, value] { SaveDynamicIslandSettingNow(key, value); });
}

static void SaveDynamicIslandMultiple(const std::vector<SettingKeyValue>& items)
{
    QueueSettingsWork([items] { SaveDynamicIslandMultipleNow(items); });
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
    header.Margin(Thickness{ 0, 0, 0, 24 });

    Border hero;
    hero.CornerRadius(CornerRadius{ 16, 16, 16, 16 });
    hero.BorderThickness(Thickness{ 1, 1, 1, 1 });
    hero.Padding(Thickness{ 0, 0, 0, 0 });
    try {
        auto resources = Application::Current().Resources();
        if (resources.HasKey(box_value(L"CardBackgroundFillColorSecondaryBrush")))
            hero.Background(resources.Lookup(box_value(L"CardBackgroundFillColorSecondaryBrush")).as<Brush>());
        else if (resources.HasKey(box_value(L"CardBackgroundFillColorDefaultBrush")))
            hero.Background(resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>());
        if (resources.HasKey(box_value(L"CardStrokeColorDefaultBrush")))
            hero.BorderBrush(resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>());
    } catch (...) {}

    Grid layout;
    ColumnDefinition accentColumn;
    accentColumn.Width(GridLength{ 4.0, GridUnitType::Pixel });
    ColumnDefinition contentColumn;
    contentColumn.Width(GridLength{ 1.0, GridUnitType::Star });
    layout.ColumnDefinitions().Append(accentColumn);
    layout.ColumnDefinitions().Append(contentColumn);
    Border accent;
    try {
        auto resources = Application::Current().Resources();
        if (resources.HasKey(box_value(L"AccentFillColorDefaultBrush")))
            accent.Background(resources.Lookup(box_value(L"AccentFillColorDefaultBrush")).as<Brush>());
    } catch (...) {}
    Grid::SetColumn(accent, 0);
    layout.Children().Append(accent);

    StackPanel content;
    content.Spacing(7);
    content.Padding(Thickness{ 24, 20, 24, 20 });
    TextBlock title;
    title.Text(title_str);
    title.FontSize(30.0);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    title.TextWrapping(TextWrapping::Wrap);
    content.Children().Append(title);

    TextBlock subtitle;
    subtitle.Text(subtitle_str);
    subtitle.FontSize(14.0);
    subtitle.Opacity(0.8);
    subtitle.TextWrapping(TextWrapping::Wrap);
    subtitle.MaxWidth(760);
    content.Children().Append(subtitle);
    Grid::SetColumn(content, 1);
    layout.Children().Append(content);
    hero.Child(layout);
    header.Children().Append(hero);

    return header;
}

static TextBlock CreateSectionHeader(const std::wstring& sec_title)
{
    TextBlock header;
    header.Text(sec_title);
    header.FontSize(18.0);
    header.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    header.Margin(Thickness{ 0, 24, 0, 10 });
    return header;
}

static Border CreateSettingsCard(const std::wstring& title, const std::wstring& desc, FrameworkElement const& control, bool vertical_layout = false)
{
    Border card;
    card.CornerRadius(CornerRadius{ 14, 14, 14, 14 });
    card.BorderThickness(Thickness{ 1, 1, 1, 1 });
    card.Margin(Thickness{ 0, 0, 0, 12 });
    card.Padding(Thickness{ 22, 18, 22, 18 });
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(control, title);

    try {
        auto res = Application::Current().Resources();
        if (res.HasKey(box_value(L"CardBackgroundFillColorSecondaryBrush"))) {
            card.Background(res.Lookup(box_value(L"CardBackgroundFillColorSecondaryBrush")).as<Brush>());
        } else if (res.HasKey(box_value(L"CardBackgroundFillColorDefaultBrush"))) {
            card.Background(res.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>());
        }
        if (res.HasKey(box_value(L"CardStrokeColorDefaultBrush"))) {
            card.BorderBrush(res.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>());
        }
    } catch (...) {}

    if (vertical_layout) {
        StackPanel vPanel;
        vPanel.Spacing(10);

        TextBlock titleBlock;
        titleBlock.Text(title);
        titleBlock.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        titleBlock.FontSize(16.0);
        vPanel.Children().Append(titleBlock);

        if (!desc.empty()) {
            TextBlock descBlock;
            descBlock.Text(desc);
            descBlock.FontSize(13.5);
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
        titleBlock.FontSize(16.0);
        textPanel.Children().Append(titleBlock);

        if (!desc.empty()) {
            TextBlock descBlock;
            descBlock.Text(desc);
            descBlock.FontSize(13.5);
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
    slider->Width(240);
    slider->VerticalAlignment(VerticalAlignment::Center);
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(*slider, title);

    numBox->Width(90);
    numBox->SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
    numBox->Minimum(min_v);
    numBox->Maximum(max_v);
    numBox->SmallChange(step_v);
    numBox->LargeChange(step_v * 4.0);
    numBox->Value(cur_val);
    numBox->VerticalAlignment(VerticalAlignment::Center);
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(*numBox, title);

    auto is_updating = std::make_shared<bool>(false);
    auto debounce = std::make_shared<DispatcherTimer>();
    debounce->Interval(std::chrono::milliseconds(400));
    auto pointer_dragging = std::make_shared<bool>(false);

    auto weakSlider = winrt::make_weak(*slider);
    std::weak_ptr<DispatcherTimer> weakDebounce = debounce;
    debounce->Tick([plugin_name, key, weakSlider, weakDebounce, decimal_places](IInspectable const&, IInspectable const&) {
        try {
            if (auto timer = weakDebounce.lock()) timer->Stop();
            auto control = weakSlider.get();
            if (!control) return;
            double val = control.Value();
            if (decimal_places == 0) val = std::round(val);
            SaveAndReload(plugin_name, key, cJSON_CreateNumber(val));
        } catch (...) {}
    });

    slider->ValueChanged([valLbl, numBox, is_updating, debounce, format_val, pointer_dragging](auto const&, auto const& e) {
        if (!*is_updating) {
            *is_updating = true;
            numBox->Value(e.NewValue());
            *is_updating = false;
            if (!*pointer_dragging) {
                debounce->Stop();
                debounce->Start();
            }
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

    auto persistSlider = [plugin_name, key, weakSlider, debounce, decimal_places, pointer_dragging]() {
        try {
            *pointer_dragging = false;
            debounce->Stop();
            auto control = weakSlider.get();
            if (!control) return;
            double val = control.Value();
            if (decimal_places == 0) val = std::round(val);
            SaveAndReload(plugin_name, key, cJSON_CreateNumber(val));
        } catch (...) {}
    };
    slider->PointerPressed([pointer_dragging, debounce](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
        *pointer_dragging = true;
        debounce->Stop();
    });
    slider->PointerReleased([persistSlider](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) { persistSlider(); });
    slider->PointerCaptureLost([persistSlider](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) { persistSlider(); });
    slider->LostFocus([persistSlider](IInspectable const&, RoutedEventArgs const&) { persistSlider(); });

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
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(*slider, title);

    numBox->Width(90);
    numBox->SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
    numBox->Minimum(min_v);
    numBox->Maximum(max_v);
    numBox->SmallChange(step_v);
    numBox->LargeChange(step_v * 4.0);
    numBox->Value(cur_val);
    numBox->VerticalAlignment(VerticalAlignment::Center);
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(*numBox, title);

    auto is_updating = std::make_shared<bool>(false);
    auto debounce = std::make_shared<DispatcherTimer>();
    debounce->Interval(std::chrono::milliseconds(400));
    auto pointer_dragging = std::make_shared<bool>(false);
    auto weakSlider = winrt::make_weak(*slider);
    std::weak_ptr<DispatcherTimer> weakDebounce = debounce;

    slider->ValueChanged([valLbl, numBox, is_updating, debounce, format_val, pointer_dragging](auto const&, auto const& e) {
        if (!*is_updating) {
            *is_updating = true;
            numBox->Value(e.NewValue());
            *is_updating = false;
            if (!*pointer_dragging) {
                debounce->Stop();
                debounce->Start();
            }
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

    debounce->Tick([key, weakSlider, weakDebounce](IInspectable const&, IInspectable const&) {
        try {
            if (auto timer = weakDebounce.lock()) timer->Stop();
            auto control = weakSlider.get();
            if (!control) return;
            SaveDynamicIslandSetting(key, cJSON_CreateNumber(std::round(control.Value())));
        } catch (...) {}
    });

    auto persistSlider = [key, weakSlider, debounce, pointer_dragging]() {
        try {
            *pointer_dragging = false;
            debounce->Stop();
            auto control = weakSlider.get();
            if (!control) return;
            SaveDynamicIslandSetting(key, cJSON_CreateNumber(std::round(control.Value())));
        } catch (...) {}
    };
    slider->PointerPressed([pointer_dragging, debounce](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
        *pointer_dragging = true;
        debounce->Stop();
    });
    slider->PointerReleased([persistSlider](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) { persistSlider(); });
    slider->PointerCaptureLost([persistSlider](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) { persistSlider(); });
    slider->LostFocus([persistSlider](IInspectable const&, RoutedEventArgs const&) { persistSlider(); });

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
            QueueSettingsWork([plugin_name, is_on] {
                if (is_on) GuiIpcEnablePlugin(plugin_name);
                else GuiIpcDisablePlugin(plugin_name);
            });
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
    actionPanel.Spacing(12);
    actionPanel.Margin(Thickness{ 0, 16, 0, 24 });
    actionPanel.VerticalAlignment(VerticalAlignment::Center);

    Button applyBtn;
    applyBtn.Content(box_value(btn_text));
    applyBtn.Padding(Thickness{ 22, 10, 22, 10 });
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
    auto token = std::make_shared<winrt::event_token>();
    *token = timer->Tick([lbl, timer, token](IInspectable const&, IInspectable const&) {
        try {
            timer->Stop();
            timer->Tick(*token);
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
    panel.Padding(Thickness{ 36, 28, 36, 36 });
    panel.MaxWidth(1040);
    panel.HorizontalAlignment(HorizontalAlignment::Center);

    panel.Children().Append(CreatePageHeader(
        L"Taskbar size",
        L"Set the taskbar height and icon spacing, or return to the Windows default."
    ));

    const std::string plugin_name = "taskbar_resize";

    // 1. Master Toggle Card
    auto masterToggle = std::make_shared<ToggleSwitch>();
    panel.Children().Append(CreateToggleSettingCard(
        L"Custom taskbar size",
        L"Turn this on to use the size and spacing settings below.",
        plugin_name, "enabled", true, masterToggle
    ));

    panel.Children().Append(CreateSectionHeader(L"Presets"));

    // 2. The "Default Taskbar" Option Card
    Button defaultTaskbarBtn;
    defaultTaskbarBtn.Content(box_value(L"Restore Default Taskbar"));
    defaultTaskbarBtn.Padding(Thickness{ 16, 8, 16, 8 });
    defaultTaskbarBtn.VerticalAlignment(VerticalAlignment::Center);
    defaultTaskbarBtn.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

    Border defaultCard = CreateSettingsCard(
        L"Restore Windows default",
        L"Set the taskbar to its standard 48 px height and turn off custom sizing.",
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
        L"Quick sizes",
        L"Choose a ready-made taskbar size.",
        presetsRow
    ));

    // 4. Sliders for Custom Dimensions
    panel.Children().Append(CreateSectionHeader(L"Fine tune"));

    auto heightCtrl = CreateSliderSettingCard(
        L"Taskbar Height",
        L"Set the taskbar height.",
        plugin_name, "height", 20.0, 128.0, 1.0, 48.0, L"px"
    );
    panel.Children().Append(heightCtrl.card);

    auto padTopCtrl = CreateSliderSettingCard(
        L"Top Padding Offset",
        L"Move taskbar icons down or up.",
        plugin_name, "padding_top", -20.0, 20.0, 1.0, 0.0, L"px"
    );
    panel.Children().Append(padTopCtrl.card);

    auto padBotCtrl = CreateSliderSettingCard(
        L"Bottom Padding Offset",
        L"Adjust the space below taskbar icons.",
        plugin_name, "padding_bottom", -20.0, 20.0, 1.0, 0.0, L"px"
    );
    panel.Children().Append(padBotCtrl.card);

    auto spacingCtrl = CreateSliderSettingCard(
        L"Icon Spacing",
        L"Set the gap between taskbar icons.",
        plugin_name, "icon_spacing", 0.0, 32.0, 1.0, 0.0, L"px"
    );
    panel.Children().Append(spacingCtrl.card);

    // Action Bar
    auto actionBar = CreateActionBar(L"Apply taskbar size");
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
        QueueSettingsWork([plugin_name] { GuiIpcEnablePlugin(plugin_name); });
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
        QueueSettingsWork([plugin_name] { GuiIpcDisablePlugin(plugin_name); });
        ShowTimedStatus(actionBar.statusLbl, L"Taskbar restored to the Windows default.");
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
        QueueSettingsWork([plugin_name] { GuiIpcDisablePlugin(plugin_name); });
        ShowTimedStatus(actionBar.statusLbl, L"Taskbar restored to the Windows default.");
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
        QueueSettingsWork([plugin_name] { GuiIpcEnablePlugin(plugin_name); });
        ShowTimedStatus(actionBar.statusLbl, L"Taskbar size saved and applied.");
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
    panel.Padding(Thickness{ 36, 28, 36, 36 });
    panel.MaxWidth(1040);
    panel.HorizontalAlignment(HorizontalAlignment::Center);

    panel.Children().Append(CreatePageHeader(
        L"Icon magnification",
        L"Choose how taskbar icons grow and move when you point to them."
    ));

    const std::string plugin_name = "icon_hover";

    // Master Toggle
    auto masterToggle = std::make_shared<ToggleSwitch>();
    panel.Children().Append(CreateToggleSettingCard(
        L"Magnify icons on hover",
        L"Turn icon magnification on or off.",
        plugin_name, "enabled", true, masterToggle
    ));

    panel.Children().Append(CreateSectionHeader(L"Size and range"));

    auto scaleCtrl = CreateSliderSettingCard(
        L"Maximum icon size",
        L"Set how large an icon can grow.",
        plugin_name, "max_scale", 1.0, 10.0, 0.05, 6.5, L"x", 2
    );
    panel.Children().Append(scaleCtrl.card);

    auto radiusCtrl = CreateSliderSettingCard(
        L"Magnification range",
        L"Set how far the effect reaches across nearby icons.",
        plugin_name, "radius", 50.0, 500.0, 5.0, 190.0, L"px"
    );
    panel.Children().Append(radiusCtrl.card);

    auto speedCtrl = CreateSliderSettingCard(
        L"Animation speed",
        L"Set how quickly icons return to their normal size.",
        plugin_name, "speed_ms", 50.0, 500.0, 10.0, 500.0, L"ms"
    );
    panel.Children().Append(speedCtrl.card);

    panel.Children().Append(CreateSectionHeader(L"Motion"));

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
        L"Motion curve",
        L"Choose how the size change eases between nearby icons.",
        curveCombo
    ));

    panel.Children().Append(CreateToggleSettingCard(
        L"Keep magnification visible",
        L"Keep the effect in front of other taskbar content.",
        plugin_name, "keep_on_top", true
    ));

    auto actionBar = CreateActionBar(L"Apply icon settings");
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
        ShowTimedStatus(actionBar.statusLbl, L"Icon settings saved and applied.");
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
    panel.Padding(Thickness{ 36, 28, 36, 36 });
    panel.MaxWidth(1040);
    panel.HorizontalAlignment(HorizontalAlignment::Center);

    panel.Children().Append(CreatePageHeader(
        L"Motion and gestures",
        L"Adjust bounce, tilt, and drag feedback for taskbar icons."
    ));

    const std::string plugin_name = "icon_hover";

    // SECTION 1: Notification Spring Bouncing
    panel.Children().Append(CreateSectionHeader(L"Notification bounce"));

    panel.Children().Append(CreateToggleSettingCard(
        L"Bounce on notification",
        L"Animate an app icon when it receives a notification.",
        plugin_name, "bounce_enabled", true
    ));

    auto bounceStrengthCtrl = CreateSliderSettingCard(
        L"Bounce strength",
        L"Set how strongly the icon bounces.",
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
            auto token = std::make_shared<winrt::event_token>();
            *token = timer->Tick([timer, token, testStatus](IInspectable const&, IInspectable const&) {
            try {
                timer->Stop();
                    timer->Tick(*token);
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
    panel.Children().Append(CreateSectionHeader(L"Tilt"));

    panel.Children().Append(CreateToggleSettingCard(
        L"Tilt icons on hover",
        L"Add a subtle tilt as the pointer moves across an icon.",
        plugin_name, "tilt_enabled", true
    ));

    auto tiltAngleCtrl = CreateSliderSettingCard(
        L"Tilt amount",
        L"Set the maximum icon tilt.",
        plugin_name, "max_tilt_angle", 0.0, 45.0, 1.0, 20.0, L"°"
    );
    panel.Children().Append(tiltAngleCtrl.card);

    // SECTION 3: Drag & Drop Physics
    panel.Children().Append(CreateSectionHeader(L"Drag and drop"));

    panel.Children().Append(CreateToggleSettingCard(
        L"Animate dragged icons",
        L"Shrink the icon being dragged and make room around it.",
        plugin_name, "drag_drop_enabled", true
    ));

    auto dragRecessionCtrl = CreateSliderSettingCard(
        L"Dragged icon size",
        L"Set the size of an icon while you drag it.",
        plugin_name, "drag_recession_scale", 0.50, 1.00, 0.05, 0.65, L"x", 2
    );
    panel.Children().Append(dragRecessionCtrl.card);

    auto dropPushCtrl = CreateSliderSettingCard(
        L"Space around dragged icon",
        L"Set how far nearby icons move aside.",
        plugin_name, "drop_zone_push", 10.0, 120.0, 5.0, 50.0, L"px"
    );
    panel.Children().Append(dropPushCtrl.card);

    auto actionBar = CreateActionBar(L"Apply motion settings");
    panel.Children().Append(actionBar.panel);

    actionBar.applyBtn.Click([plugin_name, bounceStrengthCtrl, tiltAngleCtrl, dragRecessionCtrl, dropPushCtrl, actionBar](IInspectable const&, RoutedEventArgs const&) {
        std::vector<SettingKeyValue> items;
        items.push_back({ "bounce_strength", cJSON_CreateNumber(bounceStrengthCtrl.slider->Value()) });
        items.push_back({ "max_tilt_angle", cJSON_CreateNumber(std::round(tiltAngleCtrl.slider->Value())) });
        items.push_back({ "drag_recession_scale", cJSON_CreateNumber(dragRecessionCtrl.slider->Value()) });
        items.push_back({ "drop_zone_push", cJSON_CreateNumber(std::round(dropPushCtrl.slider->Value())) });

        SaveMultipleAndReload(plugin_name, items);
        ShowTimedStatus(actionBar.statusLbl, L"Motion settings saved and applied.");
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
    panel.Padding(Thickness{ 36, 28, 36, 36 });
    panel.MaxWidth(1040);
    panel.HorizontalAlignment(HorizontalAlignment::Center);

    panel.Children().Append(CreatePageHeader(
        L"Start button",
        L"Choose an image for the Start button or restore the Windows icon."
    ));

    const std::string plugin_name = "icon_hover";

    panel.Children().Append(CreateSectionHeader(L"Button image"));

    auto start_image_box = std::make_shared<TextBox>();
    std::string cur_img = GetStringSetting(plugin_name, "start_image_path", "");
    start_image_box->Text(to_hstring(cur_img));
    start_image_box->PlaceholderText(L"Choose an image file");
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
        L"Image file",
        L"Choose a PNG, SVG, JPG, BMP, or ICO file.",
        inputRow
    ));

    TextBlock infoBlock;
    infoBlock.Text(L"The Start button keeps its normal click behavior and works with icon magnification and motion settings.");
    infoBlock.FontSize(12.0);
    infoBlock.Opacity(0.72);
    infoBlock.TextWrapping(TextWrapping::Wrap);

    Border infoCard;
    infoCard.CornerRadius(CornerRadius{ 14, 14, 14, 14 });
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

    auto actionBar = CreateActionBar(L"Apply image");
    panel.Children().Append(actionBar.panel);

    actionBar.applyBtn.Click([plugin_name, box_ptr, actionBar](IInspectable const&, RoutedEventArgs const&) {
        std::string img_path = to_string(box_ptr->Text());
        SaveAndReload(plugin_name, "start_image_path", cJSON_CreateString(img_path.c_str()));
        ShowTimedStatus(actionBar.statusLbl, L"Start button image saved and applied.");
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
    panel.Padding(Thickness{ 36, 28, 36, 36 });
    panel.MaxWidth(1040);
    panel.HorizontalAlignment(HorizontalAlignment::Center);

    panel.Children().Append(CreatePageHeader(
        L"Media pill",
        L"Show playback controls and track details beside the taskbar notification area."
    ));

    Border guideCard;
    guideCard.CornerRadius(CornerRadius{ 14, 14, 14, 14 });
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
    guideTitle.Text(L"How it works");
    guideTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    guideTitle.FontSize(13.5);
    guideStack.Children().Append(guideTitle);

    TextBlock guideBody;
    guideBody.Text(L"The pill appears while media is playing. Point to it to see track details, or select it to play or pause. Choose whether it stays visible when idle.");
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
        L"Show media pill",
        L"Turn the media pill on or off.",
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
        L"When media is idle",
        L"Choose whether the pill stays visible or appears only when you point to it.",
        idlePillToggle
    ));

    panel.Children().Append(CreateSectionHeader(L"Size and spacing"));

    auto padTrayCtrl = CreateDynamicIslandSliderSettingCard(
        L"Space from notification area",
        L"Set the gap between the pill and the notification area.",
        "padding_tray", 0.0, 48.0, 1.0, 15.0, L"px"
    );
    panel.Children().Append(padTrayCtrl.card);

    auto compactCtrl = CreateDynamicIslandSliderSettingCard(
        L"Compact width",
        L"Set the width when the pill is not expanded.",
        "compact_width", 40.0, 160.0, 2.0, 80.0, L"px"
    );
    panel.Children().Append(compactCtrl.card);

    auto expandedCtrl = CreateDynamicIslandSliderSettingCard(
        L"Expanded width",
        L"Set the width used to show track details.",
        "expanded_width", 160.0, 400.0, 5.0, 240.0, L"px"
    );
    panel.Children().Append(expandedCtrl.card);

    auto heightCtrl = CreateDynamicIslandSliderSettingCard(
        L"Height",
        L"Set the height of the media pill.",
        "height", 20.0, 48.0, 1.0, 30.0, L"px"
    );
    panel.Children().Append(heightCtrl.card);

    auto actionBar = CreateActionBar(L"Apply media pill size");
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
        ShowTimedStatus(actionBar.statusLbl, L"Media pill settings saved and applied.");
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
    const std::vector<WidgetCatalogItem> widget_catalog = plugin_name == "widget_host" ? ReadWidgetCatalog() : std::vector<WidgetCatalogItem>{};
    std::unordered_map<std::string, std::string> widget_names;
    for (const auto& widget : widget_catalog) widget_names.emplace(widget.id, widget.name);
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(12);
    panel.Padding(Thickness{ 32, 28, 32, 32 });
    panel.MaxWidth(1040);
    panel.HorizontalAlignment(HorizontalAlignment::Center);
    
    if (plugin_name == "widget_host") {
        panel.Children().Append(CreatePageHeader(
            L"Widgets", L"Choose what appears on your taskbar, then arrange it the way you like."));
    } else {
        std::string page_title = plugin_name;
        for (auto& character : page_title) {
            if (character == '_' || character == '-') character = ' ';
        }
        if (!page_title.empty()) page_title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(page_title[0])));
        panel.Children().Append(CreatePageHeader(to_hstring(page_title).c_str(),
            L"Changes save automatically and apply while TaskbarEngine is running."));
    }

    if (!GuiIpcIsConnected()) {
        Border stateCard;
        stateCard.CornerRadius(CornerRadius{ 10, 10, 10, 10 });
        stateCard.Padding(Thickness{ 16, 14, 16, 14 });
        stateCard.Margin(Thickness{ 0, 0, 0, 14 });
        StackPanel stateContent;
        stateContent.Spacing(4);
        TextBlock stateTitle;
        stateTitle.Text(L"Engine is offline");
        stateTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        stateContent.Children().Append(stateTitle);
        TextBlock stateText;
        stateText.Text(L"You can still change settings. They will apply when the engine is running.");
        stateText.TextWrapping(TextWrapping::Wrap);
        stateText.Opacity(0.76);
        stateContent.Children().Append(stateText);
        stateCard.Child(stateContent);
        panel.Children().Append(stateCard);
    }

    if (plugin_name == "widget_host") {
        panel.Children().Append(CreateSectionHeader(L"Widget bar"));
        panel.Children().Append(CreateToggleSettingCard(
            L"Show widgets on the taskbar", L"Turn the complete widget bar on or off.", "widget_host", "enabled", true));

        StackPanel orderingPanel;
        orderingPanel.Spacing(6);
        TextBlock orderTitle;
        orderTitle.Text(L"Choose and arrange widgets");
        orderTitle.FontSize(15.0);
        orderTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        orderingPanel.Children().Append(orderTitle);
        TextBlock orderHint;
        orderHint.Text(L"Turn a widget on or off. Use the arrows to change its left-to-right position.");
        orderHint.Opacity(0.72);
        orderHint.TextWrapping(TextWrapping::Wrap);
        orderingPanel.Children().Append(orderHint);

        std::vector<std::string> order;
        cJSON* orderValue = GetCurrentValue("widget_host", "layout");
        if (orderValue && cJSON_IsArray(orderValue)) {
            cJSON* entry = nullptr;
            cJSON_ArrayForEach(entry, orderValue) {
                if (cJSON_IsString(entry) && entry->valuestring && entry->valuestring[0]) order.emplace_back(entry->valuestring);
            }
        } else if (orderValue && cJSON_IsString(orderValue) && orderValue->valuestring) {
            std::string value = orderValue->valuestring;
            size_t begin = 0;
            while (begin < value.size()) {
                size_t end = value.find(',', begin);
                std::string item = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                if (!item.empty()) order.push_back(item);
                if (end == std::string::npos) break;
                begin = end + 1;
            }
        }
        if (orderValue) cJSON_Delete(orderValue);
        if (!widget_catalog.empty()) {
            for (const auto& widget : widget_catalog)
                if (std::find(order.begin(), order.end(), widget.id) == order.end()) order.push_back(widget.id);
        } else {
            for (const char* name : { "clock", "sysmon", "media" })
                if (std::find(order.begin(), order.end(), name) == order.end()) order.emplace_back(name);
        }
        auto orderItems = std::make_shared<std::vector<std::string>>(std::move(order));
        auto saveOrder = [orderItems]() {
            cJSON* layout = cJSON_CreateArray();
            if (!layout) return;
            for (const auto& id : *orderItems) {
                cJSON* item = cJSON_CreateString(id.c_str());
                if (!item || !cJSON_AddItemToArray(layout, item)) {
                    if (item) cJSON_Delete(item);
                    cJSON_Delete(layout);
                    return;
                }
            }
            SaveAndReload("widget_host", "layout", layout);
        };
        auto rows = std::make_shared<std::vector<StackPanel>>();
        auto refreshOrder = std::make_shared<std::function<void()>>();
        *refreshOrder = [orderingPanel, orderItems, rows, refreshOrder, widget_names, saveOrder]() {
            while (orderingPanel.Children().Size() > 2) orderingPanel.Children().RemoveAt(orderingPanel.Children().Size() - 1);
            rows->clear();
            for (size_t index = 0; index < orderItems->size(); ++index) {
                StackPanel row;
                row.Orientation(Orientation::Horizontal);
                row.Spacing(12);
                row.VerticalAlignment(VerticalAlignment::Center);
                TextBlock name;
                const auto found_name = widget_names.find((*orderItems)[index]);
                const std::string display_name = found_name != widget_names.end() ? found_name->second :
                    (*orderItems)[index] == "sysmon" ? "System monitor" :
                    (*orderItems)[index] == "clock" ? "Clock" : (*orderItems)[index] == "media" ? "Media" : (*orderItems)[index];
                name.Text(to_hstring(display_name));
                name.Width(160);
                name.VerticalAlignment(VerticalAlignment::Center);
                ToggleSwitch enabled;
                enabled.OffContent(box_value(L"Hidden"));
                enabled.OnContent(box_value(L"Shown"));
                std::wstring accessible_widget_name = L"Show ";
                accessible_widget_name += to_hstring(display_name).c_str();
                Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(enabled, accessible_widget_name);
                enabled.IsOn(GetBoolSetting("widget_host", (*orderItems)[index] + ".enabled", true));
                enabled.Toggled([key = (*orderItems)[index] + ".enabled"](IInspectable const& sender, RoutedEventArgs const&) {
                    SaveAndReload("widget_host", key, cJSON_CreateBool(sender.as<ToggleSwitch>().IsOn()));
                });
                Button up;
                up.Content(box_value(L"\u2191"));
                up.IsEnabled(index > 0);
                std::wstring move_left_name = L"Move ";
                move_left_name += to_hstring(display_name).c_str();
                move_left_name += L" left";
                Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(up, move_left_name);
                Button down;
                down.Content(box_value(L"\u2193"));
                down.IsEnabled(index + 1 < orderItems->size());
                std::wstring move_right_name = L"Move ";
                move_right_name += to_hstring(display_name).c_str();
                move_right_name += L" right";
                Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(down, move_right_name);
                up.Click([index, orderItems, saveOrder, refreshOrder](IInspectable const&, RoutedEventArgs const&) {
                    if (index == 0 || index >= orderItems->size()) return;
                    std::swap((*orderItems)[index], (*orderItems)[index - 1]);
                    saveOrder();
                    (*refreshOrder)();
                });
                down.Click([index, orderItems, saveOrder, refreshOrder](IInspectable const&, RoutedEventArgs const&) {
                    if (index + 1 >= orderItems->size()) return;
                    std::swap((*orderItems)[index], (*orderItems)[index + 1]);
                    saveOrder();
                    (*refreshOrder)();
                });
                row.Children().Append(name);
                row.Children().Append(enabled);
                row.Children().Append(up);
                row.Children().Append(down);
                Border rowCard;
                rowCard.CornerRadius(CornerRadius{ 12, 12, 12, 12 });
                rowCard.Padding(Thickness{ 14, 10, 14, 10 });
                rowCard.BorderThickness(Thickness{ 1, 1, 1, 1 });
                try {
                    auto rowResources = Application::Current().Resources();
                    if (rowResources.HasKey(box_value(L"CardBackgroundFillColorDefaultBrush")))
                        rowCard.Background(rowResources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>());
                    if (rowResources.HasKey(box_value(L"CardStrokeColorDefaultBrush")))
                        rowCard.BorderBrush(rowResources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<Brush>());
                } catch (...) {}
                rowCard.Child(row);
                orderingPanel.Children().Append(rowCard);
                rows->push_back(row);
            }
        };
        (*refreshOrder)();
        Border orderCard;
        orderCard.CornerRadius(CornerRadius{ 14, 14, 14, 14 });
        orderCard.BorderThickness(Thickness{ 1, 1, 1, 1 });
        orderCard.Padding(Thickness{ 16, 14, 16, 14 });
        orderCard.Margin(Thickness{ 0, 0, 0, 12 });
        orderCard.Child(orderingPanel);
        panel.Children().Append(orderCard);
    }

    cJSON* schema = cJSON_Parse(schema_json.c_str());
    if (schema) {
        cJSON* settings = cJSON_GetObjectItem(schema, "settings");
        if (settings && cJSON_IsArray(settings)) {
            std::string activeWidgetSection;
            StackPanel activeWidgetSettings;
            std::unordered_map<std::string, StackPanel> widgetSettingsPanels;
            cJSON* setting = nullptr;
            cJSON_ArrayForEach(setting, settings) {
                cJSON* c_key = cJSON_GetObjectItem(setting, "key");
                cJSON* c_label = cJSON_GetObjectItem(setting, "label");
                cJSON* c_type = cJSON_GetObjectItem(setting, "type");
                
                if (!c_key || !cJSON_IsString(c_key) || !c_label || !cJSON_IsString(c_label) || !c_type || !cJSON_IsString(c_type)) continue;
                
                std::string key = c_key->valuestring;
                if (plugin_name == "widget_host" &&
                    (key == "enabled" || key == "layout" || key == "clock.enabled" ||
                     key == "sysmon.enabled" || key == "media.enabled" ||
                     (key.size() > 8 && key.substr(key.size() - 8) == ".enabled" &&
                      std::any_of(widget_catalog.begin(), widget_catalog.end(), [&](const auto& widget) {
                          return key == widget.id + ".enabled";
                      })))) continue;
                if (plugin_name == "widget_host") {
                    const size_t separator = key.find('.');
                    const std::string section = separator == std::string::npos ? "general" : key.substr(0, separator);
                    if (activeWidgetSection != section) {
                        activeWidgetSection = section;
                        const auto section_name = widget_names.find(section);
                        const std::string section_title = section_name != widget_names.end() ? section_name->second :
                            section == "general" ? "Widget host" : section == "sysmon" ? "System monitor" :
                            section == "clock" ? "Clock" : section == "media" ? "Media" : section;
                        if (section == "general") {
                            panel.Children().Append(CreateSectionHeader(L"Layout"));
                        } else {
                            auto existing_group = widgetSettingsPanels.find(section);
                            if (existing_group != widgetSettingsPanels.end()) {
                                activeWidgetSettings = existing_group->second;
                            } else {
                                activeWidgetSettings = StackPanel{};
                                activeWidgetSettings.Spacing(10);
                                Expander advanced;
                                advanced.Header(box_value(to_hstring(section_title + " options")));
                                advanced.IsExpanded(false);
                                advanced.Margin(Thickness{ 0, 0, 0, 4 });
                                advanced.Content(activeWidgetSettings);
                                widgetSettingsPanels.emplace(section, activeWidgetSettings);
                                panel.Children().Append(advanced);
                            }
                        }
                    }
                }
                std::string label = c_label->valuestring;
                std::string type = c_type->valuestring;
                
                StackPanel itemPanel;
                itemPanel.Spacing(8);
                
                cJSON* tooltip = cJSON_GetObjectItem(setting, "tooltip");
                const std::wstring description = tooltip && cJSON_IsString(tooltip) && tooltip->valuestring
                    ? to_hstring(tooltip->valuestring).c_str() : L"";
                
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

                    double val = min_val;
                    if (current && cJSON_IsNumber(current)) val = current->valuedouble;
                    else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) val = def->valuedouble;
                    val = std::clamp(val, min_val, max_val);

                    Slider slider;
                    slider.Minimum(min_val);
                    slider.Maximum(max_val);
                    slider.StepFrequency(step_val);
                    slider.SmallChange(step_val);
                    slider.Value(val);
                    slider.IsThumbToolTipEnabled(true);
                    slider.Width(280);
                    slider.MinWidth(180);
                    slider.MaxWidth(420);
                    slider.HorizontalAlignment(HorizontalAlignment::Stretch);
                    const std::wstring label_text = to_hstring(label).c_str();
                    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(slider, label_text);

                    TextBlock valueLabel;
                    valueLabel.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                    valueLabel.MinWidth(64);
                    valueLabel.TextAlignment(TextAlignment::Right);
                    valueLabel.VerticalAlignment(VerticalAlignment::Center);
                    cJSON* unit_value = cJSON_GetObjectItem(setting, "unit");
                    const std::string unit = cJSON_IsString(unit_value) && unit_value->valuestring ? unit_value->valuestring :
                        (plugin_name == "widget_host" && key == "gap" ? "DIP" : "");
                    auto update_value_label = [valueLabel, type, unit](double value) {
                        char number[32]{};
                        if (type == "int") snprintf(number, sizeof(number), "%d", static_cast<int>(std::round(value)));
                        else snprintf(number, sizeof(number), "%.1f", value);
                        std::wstring display = to_hstring(number).c_str();
                        if (!unit.empty()) {
                            display += L" ";
                            display += to_hstring(unit).c_str();
                        }
                        valueLabel.Text(display);
                    };
                    update_value_label(val);

                    auto debounce = std::make_shared<DispatcherTimer>();
                    debounce->Interval(std::chrono::milliseconds(400));
                    auto pointer_dragging = std::make_shared<bool>(false);
                    auto weakSlider = winrt::make_weak(slider);
                    std::weak_ptr<DispatcherTimer> weakDebounce = debounce;
                    debounce->Tick([plugin_name, key, type, weakSlider, weakDebounce, pointer_dragging](IInspectable const&, IInspectable const&) {
                        try {
                            if (*pointer_dragging) return;
                            if (auto timer = weakDebounce.lock()) timer->Stop();
                            auto control = weakSlider.get();
                            if (!control) return;
                            double value = control.Value();
                            if (std::isnan(value)) return;
                            if (type == "int") value = std::round(value);
                            SaveAndReload(plugin_name, key, cJSON_CreateNumber(value));
                        } catch (...) {}
                    });
                    slider.PointerPressed([pointer_dragging, debounce](auto const&, auto const&) {
                        *pointer_dragging = true;
                        debounce->Stop();
                    });
                    auto persist_slider_edit = [plugin_name, key, type, weakSlider, debounce, pointer_dragging]() {
                        *pointer_dragging = false;
                        debounce->Stop();
                        try {
                            auto control = weakSlider.get();
                            if (!control) return;
                            double value = control.Value();
                            if (std::isnan(value)) return;
                            if (type == "int") value = std::round(value);
                            SaveAndReload(plugin_name, key, cJSON_CreateNumber(value));
                        } catch (...) {}
                    };
                    slider.PointerReleased([persist_slider_edit](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
                        persist_slider_edit();
                    });
                    slider.PointerCaptureLost([persist_slider_edit](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
                        persist_slider_edit();
                    });
                    slider.ValueChanged([debounce, update_value_label, pointer_dragging](auto const&, auto const& args) {
                        update_value_label(args.NewValue());
                        if (!*pointer_dragging) {
                            debounce->Stop();
                            debounce->Start();
                        }
                    });
                    StackPanel sliderRow;
                    sliderRow.Orientation(Orientation::Horizontal);
                    sliderRow.Spacing(10);
                    sliderRow.VerticalAlignment(VerticalAlignment::Center);
                    sliderRow.Children().Append(slider);
                    sliderRow.Children().Append(valueLabel);
                    itemPanel.Children().Append(sliderRow);
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
                    } else if (current && cJSON_IsString(current) && current->valuestring) {
                        const char* text = current->valuestring;
                        if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
                        else if (text[0] == '#') ++text;
                        val_color = static_cast<uint32_t>(strtoul(text, nullptr, 16));
                    } else if (cJSON* def = cJSON_GetObjectItem(setting, "default")) {
                        if (cJSON_IsNumber(def)) val_color = (uint32_t)def->valuedouble;
                    }
                    
                    winrt::Windows::UI::Color c;
                    c.A = static_cast<uint8_t>((val_color >> 24) & 0xFF);
                    c.R = static_cast<uint8_t>((val_color >> 16) & 0xFF);
                    c.G = static_cast<uint8_t>((val_color >> 8) & 0xFF);
                    c.B = static_cast<uint8_t>(val_color & 0xFF);
                    picker.Color(c);

                    auto debounce = std::make_shared<DispatcherTimer>();
                    debounce->Interval(std::chrono::milliseconds(140));
                    auto weakPicker = winrt::make_weak(picker);
                    std::weak_ptr<DispatcherTimer> weakDebounce = debounce;
                    debounce->Tick([plugin_name, key, weakPicker, weakDebounce](IInspectable const&, IInspectable const&) {
                        try {
                            if (auto timer = weakDebounce.lock()) timer->Stop();
                            auto control = weakPicker.get();
                            if (!control) return;
                            auto clr = control.Color();
                            uint32_t argb = ((uint32_t)clr.A << 24) | ((uint32_t)clr.R << 16) | ((uint32_t)clr.G << 8) | (uint32_t)clr.B;
                            SaveAndReload(plugin_name, key, cJSON_CreateNumber(argb));
                        } catch (...) {}
                    });
                    picker.ColorChanged([debounce](ColorPicker const&, ColorChangedEventArgs const&) {
                        debounce->Stop();
                        debounce->Start();
                    });
                    itemPanel.Children().Append(picker);
                }

                if (current) cJSON_Delete(current);
                Border settingCard = CreateSettingsCard(to_hstring(label).c_str(), description,
                    itemPanel, type == "color");
                if (plugin_name == "widget_host" && activeWidgetSection != "general")
                    activeWidgetSettings.Children().Append(settingCard);
                else
                    panel.Children().Append(settingCard);
            }
        }
        cJSON_Delete(schema);
    }

    cJSON* parsedSchema = cJSON_Parse(schema_json.c_str());
    const cJSON* parsedSettings = parsedSchema ? cJSON_GetObjectItem(parsedSchema, "settings") : nullptr;
    if (!parsedSettings || !cJSON_IsArray(parsedSettings) || cJSON_GetArraySize(parsedSettings) == 0) {
        TextBlock emptyState;
        emptyState.Text(GuiIpcIsConnected()
            ? L"Settings for this feature could not be loaded. Check the engine log for details."
            : L"The engine is offline. Settings will be available when it is running.");
        emptyState.TextWrapping(TextWrapping::Wrap);
        emptyState.Opacity(0.75);
        emptyState.Margin(Thickness{ 0, 8, 0, 0 });
        panel.Children().Append(emptyState);
    }
    if (parsedSchema) cJSON_Delete(parsedSchema);
    
    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

static Page CreateFrostedGlassPage()
{
    constexpr const char* plugin = "taskbar_transparency";
    Page page;
    ScrollViewer scroll;
    StackPanel panel;
    panel.Spacing(8);
    panel.Padding(Thickness{ 32, 28, 32, 32 });
    panel.MaxWidth(1040);
    panel.HorizontalAlignment(HorizontalAlignment::Center);

    panel.Children().Append(CreatePageHeader(
        L"Frosted Glass",
        L"Choose the taskbar material, tint, and opacity. Changes are saved and applied automatically."));

    if (!GuiIpcIsConnected()) {
        TextBlock offline;
        offline.Text(L"The engine is offline. Settings can still be saved and will apply when it is running.");
        offline.TextWrapping(TextWrapping::Wrap);
        offline.Opacity(0.76);
        offline.Margin(Thickness{ 0, 0, 0, 8 });
        panel.Children().Append(offline);
    }

    panel.Children().Append(CreateSectionHeader(L"Appearance"));
    panel.Children().Append(CreateToggleSettingCard(
        L"Enable frosted glass", L"Apply the selected material to the taskbar.", plugin, "enabled", true));

    ComboBox mode;
    mode.MinWidth(190);
    mode.Items().Append(box_value(L"Clear"));
    mode.Items().Append(box_value(L"Blur"));
    mode.Items().Append(box_value(L"Acrylic"));
    mode.Items().Append(box_value(L"Tint"));
    mode.Items().Append(box_value(L"Disabled"));
    const std::vector<std::string> modes{ "clear", "blur", "acrylic", "tint", "disabled" };
    const std::string currentMode = GetStringSetting(plugin, "mode", "acrylic");
    int selectedMode = 2;
    for (size_t i = 0; i < modes.size(); ++i) {
        if (modes[i] == currentMode) selectedMode = static_cast<int>(i);
    }
    mode.SelectedIndex(selectedMode);
    mode.SelectionChanged([modes, plugin](IInspectable const& sender, SelectionChangedEventArgs const&) {
        const int index = sender.as<ComboBox>().SelectedIndex();
        if (index >= 0 && static_cast<size_t>(index) < modes.size())
            SaveAndReload(plugin, "mode", cJSON_CreateString(modes[index].c_str()));
    });
    panel.Children().Append(CreateSettingsCard(
        L"Material", L"Choose clear, blur, acrylic, or a solid tint.", mode));

    const double opacity = std::clamp(GetDoubleSetting(plugin, "opacity", 0.65), 0.0, 1.0);
    auto opacitySlider = std::make_shared<Slider>();
    auto opacityLabel = std::make_shared<TextBlock>();
    auto opacityTimer = std::make_shared<DispatcherTimer>();
    opacityTimer->Interval(std::chrono::milliseconds(160));
    opacitySlider->Minimum(0);
    opacitySlider->Maximum(100);
    opacitySlider->StepFrequency(1);
    opacitySlider->SmallChange(1);
    opacitySlider->LargeChange(10);
    opacitySlider->Value(std::round(opacity * 100.0));
    opacitySlider->IsThumbToolTipEnabled(true);
    opacitySlider->Width(320);
    opacitySlider->MinWidth(200);
    opacitySlider->MaxWidth(620);
    opacitySlider->HorizontalAlignment(HorizontalAlignment::Stretch);
    opacityLabel->FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    opacityLabel->MinWidth(52);
    opacityLabel->VerticalAlignment(VerticalAlignment::Center);
    auto updateOpacityLabel = [opacityLabel](double value) {
        wchar_t text[16];
        swprintf_s(text, L"%d%%", static_cast<int>(std::round(value)));
        opacityLabel->Text(text);
    };
    updateOpacityLabel(opacitySlider->Value());
    auto weakOpacitySlider = winrt::make_weak(*opacitySlider);
    std::weak_ptr<DispatcherTimer> weakOpacityTimer = opacityTimer;
    opacityTimer->Tick([weakOpacitySlider, weakOpacityTimer, plugin](IInspectable const&, IInspectable const&) {
        try {
            if (auto timer = weakOpacityTimer.lock()) timer->Stop();
            auto slider = weakOpacitySlider.get();
            if (slider) SaveAndReload(plugin, "opacity", cJSON_CreateNumber(slider.Value() / 100.0));
        } catch (...) {}
    });
    opacitySlider->ValueChanged([opacityTimer, updateOpacityLabel](IInspectable const&, auto const& args) {
        updateOpacityLabel(args.NewValue());
        opacityTimer->Stop();
        opacityTimer->Start();
    });
    opacitySlider->LostFocus([opacityTimer, weakOpacitySlider, plugin](IInspectable const&, RoutedEventArgs const&) {
        try {
            opacityTimer->Stop();
            auto slider = weakOpacitySlider.get();
            if (slider) SaveAndReload(plugin, "opacity", cJSON_CreateNumber(slider.Value() / 100.0));
        } catch (...) {}
    });
    Grid opacityControl;
    ColumnDefinition sliderColumn;
    sliderColumn.Width(GridLength{ 1.0, GridUnitType::Star });
    ColumnDefinition valueColumn;
    valueColumn.Width(GridLength{ 0.0, GridUnitType::Auto });
    opacityControl.ColumnDefinitions().Append(sliderColumn);
    opacityControl.ColumnDefinitions().Append(valueColumn);
    Grid::SetColumn(*opacitySlider, 0);
    opacityControl.Children().Append(*opacitySlider);
    Grid::SetColumn(*opacityLabel, 1);
    opacityLabel->Margin(Thickness{ 16, 0, 0, 0 });
    opacityControl.Children().Append(*opacityLabel);
    panel.Children().Append(CreateSettingsCard(
        L"Opacity", L"Adjust the strength of the acrylic or tint effect.", opacityControl, true));

    uint32_t tint = static_cast<uint32_t>(GetDoubleSetting(plugin, "color", 1140850688.0));
    ColorPicker tintPicker;
    tintPicker.IsColorSpectrumVisible(true);
    tintPicker.IsAlphaEnabled(true);
    tintPicker.IsHexInputVisible(true);
    winrt::Windows::UI::Color color{};
    color.A = static_cast<uint8_t>((tint >> 24) & 0xFF);
    color.R = static_cast<uint8_t>((tint >> 16) & 0xFF);
    color.G = static_cast<uint8_t>((tint >> 8) & 0xFF);
    color.B = static_cast<uint8_t>(tint & 0xFF);
    tintPicker.Color(color);
    auto tintTimer = std::make_shared<DispatcherTimer>();
    tintTimer->Interval(std::chrono::milliseconds(160));
    auto weakTintPicker = winrt::make_weak(tintPicker);
    std::weak_ptr<DispatcherTimer> weakTintTimer = tintTimer;
    tintTimer->Tick([weakTintPicker, weakTintTimer, plugin](IInspectable const&, IInspectable const&) {
        try {
            if (auto timer = weakTintTimer.lock()) timer->Stop();
            auto picker = weakTintPicker.get();
            if (!picker) return;
            const auto selected = picker.Color();
            const uint32_t argb = (static_cast<uint32_t>(selected.A) << 24) |
                (static_cast<uint32_t>(selected.R) << 16) |
                (static_cast<uint32_t>(selected.G) << 8) | selected.B;
            SaveAndReload(plugin, "color", cJSON_CreateNumber(argb));
        } catch (...) {}
    });
    tintPicker.ColorChanged([tintTimer](ColorPicker const&, ColorChangedEventArgs const&) {
        tintTimer->Stop();
        tintTimer->Start();
    });
    panel.Children().Append(CreateSettingsCard(
        L"Tint color", L"ARGB color used by acrylic and tint materials.", tintPicker, true));

    panel.Children().Append(CreateSectionHeader(L"Displays"));
    panel.Children().Append(CreateToggleSettingCard(
        L"Apply to secondary taskbars", L"Use the same material on taskbars on other monitors.",
        plugin, "apply_secondary", true));

    scroll.Content(panel);
    page.Content(scroll);
    return page;
}

// ============================================================================
// Router: CreateSettingsPage
// ============================================================================

Page CreateSettingsPage(const std::string& plugin_name, const std::string& schema_json)
{
    const std::string effective_schema = plugin_name == "widget_host"
        ? MergeWidgetCatalogSchema(schema_json, ReadWidgetCatalog()) : schema_json;
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
    if (plugin_name == "taskbar_transparency") {
        return CreateFrostedGlassPage();
    }

    // First-party sections remain fully configurable while Engine IPC is stopped.
    // Values are read from the same active config file used by EngineDLL.
    if (schema_json == "{}" && plugin_name == "taskbar_transparency") {
        return CreateGenericPluginSettingsPage(plugin_name,
            R"SETTINGS({"settings":[
                {"key":"enabled","label":"Enable taskbar transparency","tooltip":"Apply transparency to the taskbar surface.","type":"bool","default":true},
                {"key":"mode","label":"Visual mode","tooltip":"Choose clear, blur, acrylic, or a color tint.","type":"enum","default":"acrylic","options":["clear","blur","acrylic","tint","disabled"]},
                {"key":"opacity","label":"Opacity","tooltip":"Acrylic and tint opacity.","type":"float","default":0.65,"min":0,"max":1,"step":0.05},
                {"key":"color","label":"Tint color","tooltip":"ARGB tint used by acrylic or tint mode.","type":"color","default":1140850688},
                {"key":"accent_flags","label":"Accent flags","tooltip":"Windows accent policy flags.","type":"int","default":2,"min":0,"max":255,"step":1},
                {"key":"apply_secondary","label":"Apply to secondary monitors","tooltip":"Apply the selected effect to secondary taskbars.","type":"bool","default":true}
            ]})SETTINGS");
    }
    return CreateGenericPluginSettingsPage(plugin_name, effective_schema);
}

}
