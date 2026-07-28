#pragma once

#include "te_types.h"
#include "te_log.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct PluginContext;
struct PluginMetadata;
struct PluginSettings;

/**
 * @brief Setting value types for GUI auto-generation.
 */
typedef enum SettingType {
    TE_SETTING_BOOL,
    TE_SETTING_INT,
    TE_SETTING_FLOAT,
    TE_SETTING_STRING,
    TE_SETTING_ENUM,
    TE_SETTING_COLOR
} SettingType;

/**
 * @brief Setting descriptor structure for GUI generation.
 */
typedef struct SettingDescriptor {
    const char* key;
    const char* label;
    const char* tooltip;
    SettingType type;
    union {
        struct { bool default_val; } b;
        struct { int default_val; int min; int max; int step; } i;
        struct { float default_val; float min; float max; float step; } f;
        struct { const char* default_val; } s;
        struct { const char* default_val; const char** options; int count; } e;
        struct { uint32_t default_val; } color;
    } value;
} SettingDescriptor;

/**
 * @brief Container for plugin settings schema.
 */
typedef struct PluginSettings {
    const SettingDescriptor* descriptors;
    size_t count;
} PluginSettings;

/**
 * @brief Plugin metadata description.
 */
typedef struct PluginMetadata {
    const char* name;
    const char* version;
    const char* author;
    const char* description;
    uint32_t priority;
} PluginMetadata;

/**
 * @brief Shared inter-plugin state value type.
 */
typedef enum StateValueType {
    TE_STATE_TYPE_INT,
    TE_STATE_TYPE_FLOAT,
    TE_STATE_TYPE_BOOL,
    TE_STATE_TYPE_RECT
} StateValueType;

typedef struct StateValue {
    StateValueType type;
    union {
        int i;
        float f;
        bool b;
        RECT rect;
    } value;
} StateValue;

/* Callback function pointer types */
typedef HRESULT (*EventCallbackFunc)(uint32_t event_type, const void* event_data, void* user_data);
typedef HRESULT (*SubscribeFunc)(uint32_t event_type, EventCallbackFunc callback, void* user_data);
typedef HRESULT (*UnsubscribeFunc)(uint32_t event_type, EventCallbackFunc callback);
typedef void (*RequestRedrawFunc)(void);
typedef HRESULT (*PublishStateFunc)(const char* key, const StateValue* val);
typedef HRESULT (*QueryStateFunc)(const char* key, StateValue* out_val);

/**
 * @brief Plugin Context passed from Engine to Plugin upon initialization.
 */
typedef struct PluginContext {
    uint32_t api_version;
    HWND taskbar_hwnd;
    HMONITOR monitor;
    uint32_t dpi;
    const cJSON* config;
    LogFunc log;
    SubscribeFunc subscribe;
    UnsubscribeFunc unsubscribe;
    RequestRedrawFunc request_redraw;
    PublishStateFunc publish_state;
    QueryStateFunc query_state;
    void* core_opaque;
} PluginContext;

/**
 * @brief Pure C Plugin Interface (VTable).
 */
typedef struct PluginInterface {
    HRESULT (*Initialize)(const PluginContext* ctx);
    HRESULT (*Enable)(void);
    HRESULT (*Disable)(void);
    HRESULT (*Update)(float deltaTime);
    HRESULT (*Shutdown)(void);
    const PluginMetadata* (*GetMetadata)(void);
    const PluginSettings* (*GetSettings)(void);
} PluginInterface;

/**
 * @brief Exported entry point for Plugin DLLs.
 */
typedef const PluginInterface* (*GetPluginInterfaceFunc)(void);

#ifdef __cplusplus
}
#endif
