#include "widget_host_runtime.h"
#include <sdk/te_plugin.h>
#include <sdk/te_events.h>
#include <sdk/te_widget.h>
#include <cstring>

using namespace te::widgets;

namespace {

static const char* s_sysmon_modes[] = { "text", "bar", "both" };
static SettingDescriptor s_settings[29]{};
static PluginSettings s_plugin_settings{};
static bool s_settings_initialized = false;
static const PluginContext* s_context = nullptr;

static void SetBool(SettingDescriptor& d, const char* key, const char* label, int value) {
    d.key = key; d.label = label; d.type = TE_SETTING_BOOL; d.value.bool_val.default_val = value;
}
static void SetInt(SettingDescriptor& d, const char* key, const char* label, int value, int min, int max, int step) {
    d.key = key; d.label = label; d.type = TE_SETTING_INT;
    d.value.int_val.default_val = value; d.value.int_val.min_val = min; d.value.int_val.max_val = max; d.value.int_val.step = step;
}
static void SetFloat(SettingDescriptor& d, const char* key, const char* label, float value, float min, float max, float step) {
    d.key = key; d.label = label; d.type = TE_SETTING_FLOAT;
    d.value.float_val.default_val = value; d.value.float_val.min_val = min; d.value.float_val.max_val = max; d.value.float_val.step = step;
}
static void SetString(SettingDescriptor& d, const char* key, const char* label, const char* value) {
    d.key = key; d.label = label; d.type = TE_SETTING_STRING; d.value.string_val.default_val = value;
}
static void SetColor(SettingDescriptor& d, const char* key, const char* label, uint32_t value) {
    d.key = key; d.label = label; d.type = TE_SETTING_COLOR; d.value.color_val.default_val = value;
}
static void SetEnum(SettingDescriptor& d, const char* key, const char* label, int value, const char** options, int count) {
    d.key = key; d.label = label; d.type = TE_SETTING_ENUM;
    d.value.enum_val.default_val = value; d.value.enum_val.options = options; d.value.enum_val.option_count = count;
}

static void InitSettings() {
    if (s_settings_initialized) return;
    SetBool(s_settings[0], "enabled", "Enable widget host", 1);
    SetString(s_settings[1], "layout", "Widget order (comma separated)", "clock,sysmon,media");
    SetInt(s_settings[2], "position_percent", "Position along taskbar (%)", 100, 0, 100, 1);
    SetInt(s_settings[3], "gap", "Widget spacing (DIP)", 8, 0, 64, 1);
    SetBool(s_settings[4], "clock.enabled", "Show clock", 1);
    SetString(s_settings[5], "clock.format", "Clock format", "HH:mm");
    SetBool(s_settings[6], "clock.show_date", "Show date", 0);
    SetString(s_settings[7], "clock.date_format", "Date format", "ddd, MMM d");
    SetString(s_settings[8], "clock.font_family", "Clock font family", "Segoe UI Variable");
    SetFloat(s_settings[9], "clock.font_size", "Clock font size", 13.0f, 8.0f, 32.0f, 0.5f);
    SetInt(s_settings[10], "sysmon.update_interval_ms", "System monitor update interval", 2000, 1000, 10000, 500);
    SetEnum(s_settings[11], "sysmon.display_mode", "System monitor display", 0, s_sysmon_modes, 3);
    SetBool(s_settings[12], "sysmon.show_cpu", "Show CPU usage", 1);
    SetBool(s_settings[13], "sysmon.show_ram", "Show memory usage", 1);
    SetBool(s_settings[14], "media.enabled", "Show media widget", 1);
    SetBool(s_settings[15], "media.show_artist", "Show artist", 1);
    SetInt(s_settings[16], "media.max_width", "Media widget maximum width", 200, 120, 400, 10);
    SetColor(s_settings[17], "media.text_color", "Media text color (ARGB)", 0xffffffffu);
    SetColor(s_settings[18], "clock.text_color", "Clock text color (ARGB)", 0xffffffffu);
    SetFloat(s_settings[19], "sysmon.font_size", "System monitor font size", 12.0f, 8.0f, 32.0f, 0.5f);
    SetColor(s_settings[20], "sysmon.text_color", "System monitor text color (ARGB)", 0xffffffffu);
    SetColor(s_settings[21], "sysmon.bar_color", "System monitor bar color (ARGB)", 0xff4caf50u);
    SetColor(s_settings[22], "sysmon.bar_warning_color", "Warning bar color (ARGB)", 0xffff9800u);
    SetColor(s_settings[23], "sysmon.bar_critical_color", "Critical bar color (ARGB)", 0xfff44336u);
    SetFloat(s_settings[24], "sysmon.warning_threshold", "Warning threshold (%)", 75.0f, 0.0f, 100.0f, 1.0f);
    SetFloat(s_settings[25], "sysmon.critical_threshold", "Critical threshold (%)", 90.0f, 0.0f, 100.0f, 1.0f);
    SetFloat(s_settings[26], "media.font_size", "Media text size", 12.0f, 8.0f, 32.0f, 0.5f);
    SetString(s_settings[27], "sysmon.font_family", "System monitor font family", "Segoe UI Variable");
    SetString(s_settings[28], "media.font_family", "Media font family", "Segoe UI Variable");
    s_plugin_settings.descriptors = s_settings;
    s_plugin_settings.count = static_cast<uint32_t>(sizeof(s_settings) / sizeof(s_settings[0]));
    s_settings_initialized = true;
}

static void OnConfigEvent(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    const auto* changed = static_cast<const TE_ConfigChangedData*>(data);
    te::widgets::OnConfigChanged(changed);
}
static void OnTaskbarMouseEvent(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    te::widgets::OnTaskbarMouse(static_cast<const TE_TaskbarMouseData*>(data));
}
static void OnTaskbarClickEvent(uint32_t type, const void* data, void* user_data) {
    (void)type; (void)user_data;
    te::widgets::OnTaskbarClick(static_cast<const TE_TaskbarClickData*>(data));
}

static HRESULT InitializePlugin(const PluginContext* context) {
    s_context = context;
    if (context && context->subscribe) {
        context->subscribe(TE_EVENT_CONFIG_CHANGED, OnConfigEvent, nullptr);
        context->subscribe(TE_EVENT_TASKBAR_MOUSE, OnTaskbarMouseEvent, nullptr);
        context->subscribe(TE_EVENT_TASKBAR_CLICK, OnTaskbarClickEvent, nullptr);
    }
    return te::widgets::Initialize(context);
}
static HRESULT EnablePlugin(void) { return te::widgets::Enable(); }
static HRESULT DisablePlugin(void) { return te::widgets::Disable(); }
static HRESULT UpdatePlugin(float dt) { (void)dt; return TE_S_OK; }
static HRESULT ShutdownPlugin(void) {
    if (s_context && s_context->unsubscribe) {
        s_context->unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigEvent);
        s_context->unsubscribe(TE_EVENT_TASKBAR_MOUSE, OnTaskbarMouseEvent);
        s_context->unsubscribe(TE_EVENT_TASKBAR_CLICK, OnTaskbarClickEvent);
    }
    s_context = nullptr;
    return te::widgets::Shutdown();
}

static const PluginMetadata s_metadata = {
    "widget_host", "Taskbar Widgets", "Hosts compact inline taskbar widgets.",
    "TaskbarEngine", 100, 250, TE_API_VERSION
};

static const PluginMetadata* GetMetadata(void) { return &s_metadata; }
static const PluginSettings* GetSettings(void) { InitSettings(); return &s_plugin_settings; }

static const PluginInterface s_plugin_interface = {
    InitializePlugin, EnablePlugin, DisablePlugin, UpdatePlugin, ShutdownPlugin,
    GetMetadata, GetSettings
};

} // namespace

extern "C" TE_EXPORT const PluginInterface* GetPluginInterface(void) { return &s_plugin_interface; }
