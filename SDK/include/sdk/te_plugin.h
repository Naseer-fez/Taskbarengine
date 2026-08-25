#pragma once

#include <sdk/te_types.h>
#include <sdk/te_log.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Forward declaration of cJSON struct from parser library. */
struct cJSON;
typedef struct cJSON cJSON;

/**
 * Setting types for automatic GUI configuration control generation.
 */
typedef enum SettingType {
    TE_SETTING_BOOL = 0,    /**< Boolean toggle checkbox / switch. */
    TE_SETTING_INT,         /**< Integer slider or spin box with min/max/step. */
    TE_SETTING_FLOAT,       /**< Floating-point slider or numeric field with min/max/step. */
    TE_SETTING_STRING,      /**< Text entry field. */
    TE_SETTING_ENUM,        /**< Dropdown selector with a list of discrete options. */
    TE_SETTING_COLOR        /**< Color picker storing 32-bit ARGB/RGBA color value. */
} SettingType;

/**
 * Schema descriptor for a single configurable setting in a plugin.
 * Used by the settings GUI to dynamically render controls and validate inputs.
 */
typedef struct SettingDescriptor {
    const char* key;        /**< Unique configuration key name within the plugin's config object. */
    const char* label;      /**< User-facing label displayed in the configuration UI. */
    const char* tooltip;    /**< Descriptive tooltip explaining the setting purpose. */
    SettingType type;       /**< Data type determining which union field is populated. */
    union {
        struct { int default_val; int min_val; int max_val; int step; } int_val;
        struct { float default_val; float min_val; float max_val; float step; } float_val;
        struct { int default_val; } bool_val;
        struct { const char* default_val; } string_val;
        struct { int default_val; const char** options; int option_count; } enum_val;
        struct { uint32_t default_val; } color_val;
    } value;                /**< Type-specific defaults and validation constraints. */
} SettingDescriptor;

/**
 * Array container of setting descriptors exposed by a plugin.
 */
typedef struct PluginSettings {
    const SettingDescriptor* descriptors;   /**< Pointer to array of setting descriptors. */
    uint32_t count;                         /**< Number of descriptors in the array. */
} PluginSettings;

/**
 * Data type tag for StateValue union.
 */
typedef enum StateValueType {
    TE_STATE_INT = 0,       /**< 32-bit signed integer value. */
    TE_STATE_FLOAT,         /**< Single-precision floating point value. */
    TE_STATE_BOOL,          /**< Boolean value (0 = false, non-zero = true). */
    TE_STATE_RECT           /**< Win32 RECT structure value. */
} StateValueType;

/**
 * Variant value container for inter-plugin shared state blackboard.
 */
typedef struct StateValue {
    StateValueType type;    /**< Discriminator indicating the active union field. */
    union {
        int int_val;        /**< Integer payload when type == TE_STATE_INT. */
        float float_val;    /**< Float payload when type == TE_STATE_FLOAT. */
        int bool_val;       /**< Boolean payload when type == TE_STATE_BOOL. */
        RECT rect_val;      /**< RECT payload when type == TE_STATE_RECT. */
    } data;                 /**< Data union storing the typed state value. */
} StateValue;

/**
 * Function pointer for logging messages from within a plugin.
 *
 * @param level Severity level of the log message.
 * @param module Module or plugin name string.
 * @param message Message body string.
 */
typedef void (*LogFunc)(TE_LogLevel level, const char* module, const char* message);

/**
 * Function pointer to subscribe to an engine event.
 *
 * @param event_type Event ID to subscribe to.
 * @param callback Callback function invoked when the event occurs.
 * @param user_data Opaque pointer passed back to the callback.
 * @return TE_S_OK on success, or error HRESULT.
 */
typedef HRESULT (*SubscribeFunc)(uint32_t event_type, void (*callback)(uint32_t, const void*, void*), void* user_data);

/**
 * Function pointer to unsubscribe from an engine event.
 *
 * @param event_type Event ID previously subscribed to.
 * @param callback Callback function pointer used during subscription.
 * @return TE_S_OK on success, or error HRESULT.
 */
typedef HRESULT (*UnsubscribeFunc)(uint32_t event_type, void (*callback)(uint32_t, const void*, void*));

/**
 * Function pointer to request a taskbar window redraw / invalidate.
 */
typedef void (*RequestRedrawFunc)(void);

/**
 * Function pointer to publish a key-value pair to the inter-plugin state store.
 *
 * @param key Unique key name in the format "plugin_name.setting_key".
 * @param value Pointer to the StateValue to store.
 * @return TE_S_OK on success, or error HRESULT.
 */
typedef HRESULT (*PublishStateFunc)(const char* key, const StateValue* value);

/**
 * Function pointer to query a key-value pair from the inter-plugin state store.
 *
 * @param key Unique key name in the format "plugin_name.setting_key".
 * @param out_value Pointer to receive the retrieved StateValue.
 * @return TE_S_OK on success, or TE_E_FAIL if key not found.
 */
typedef HRESULT (*QueryStateFunc)(const char* key, StateValue* out_value);

/**
 * Function pointer to subscribe to a Win32 window message on the taskbar subclass.
 *
 * @param msg Win32 message identifier (e.g., WM_WINDOWPOSCHANGING).
 * @return TE_S_OK on success, or error HRESULT.
 */
typedef HRESULT (*SubscribeToMessageFunc)(UINT msg);

/**
 * Function pointer to unsubscribe from a Win32 window message on the taskbar subclass.
 *
 * @param msg Win32 message identifier.
 * @return TE_S_OK on success, or error HRESULT.
 */
typedef HRESULT (*UnsubscribeFromMessageFunc)(UINT msg);

/**
 * Function pointer to register a periodic UI thread timer.
 *
 * @param interval_ms Interval between ticks in milliseconds.
 * @param callback Callback function invoked on timer tick on the UI thread.
 * @param user_data Opaque pointer passed back to the timer callback.
 * @param out_timer_id Pointer to receive the assigned timer identifier.
 * @return TE_S_OK on success, or error HRESULT.
 */
typedef HRESULT (*RegisterTimerFunc)(uint32_t interval_ms, void (*callback)(void*), void* user_data, uint32_t* out_timer_id);

/**
 * Function pointer to cancel an active UI thread timer.
 *
 * @param timer_id Timer identifier returned by RegisterTimerFunc.
 * @return TE_S_OK on success, or error HRESULT.
 */
typedef HRESULT (*CancelTimerFunc)(uint32_t timer_id);

/**
 * Execution context and host engine services provided to plugins during Initialize().
 * Uses struct_size as an ABI envelope to allow backward/forward compatible extension.
 */
typedef struct PluginContext {
    /* ABI envelope */
    uint32_t struct_size;                           /**< sizeof(PluginContext) for version negotiation. */
    uint32_t api_version;                           /**< TE_API_VERSION supported by the host engine. */

    /* v1 fields (frozen ABI) */
    HWND taskbar_hwnd;                              /**< Window handle of the primary Shell_TrayWnd. */
    HMONITOR monitor;                               /**< Handle to the monitor hosting this taskbar. */
    uint32_t dpi;                                   /**< Current DPI scaling factor of the taskbar display. */
    const struct cJSON* config;                     /**< Read-only pointer to plugin's parsed configuration sub-tree. */
    LogFunc log;                                    /**< Host logging callback function. */
    SubscribeFunc subscribe;                        /**< Event subscription callback. */
    UnsubscribeFunc unsubscribe;                    /**< Event unsubscription callback. */
    RequestRedrawFunc request_redraw;               /**< Redraw request callback. */
    PublishStateFunc publish_state;                 /**< State store publish function. */
    QueryStateFunc query_state;                     /**< State store query function. */
    void* core_opaque;                              /**< Opaque internal pointer reserved for host engine use. */

    /* v2 fields (appended, check presence via TE_CTX_HAS_FIELD) */
    SubscribeToMessageFunc subscribe_message;       /**< Subclass window message filter registration. */
    UnsubscribeFromMessageFunc unsubscribe_message; /**< Subclass window message filter unregistration. */
    RegisterTimerFunc register_timer;               /**< UI thread periodic timer registration. */
    CancelTimerFunc cancel_timer;                   /**< UI thread periodic timer cancellation. */
} PluginContext;

/**
 * Metadata descriptor providing identity, ordering, and compatibility info for a plugin.
 */
typedef struct PluginMetadata {
    const char* name;           /**< Unique programmatic plugin identifier (e.g., "taskbar_resize"). */
    const char* display_name;   /**< Human-readable name for UI display (e.g., "Taskbar Resize"). */
    const char* description;    /**< Short description of the plugin's functionality. */
    const char* author;         /**< Author or organization name. */
    uint32_t version;           /**< Plugin version integer (e.g., 100 for 1.0.0). */
    uint32_t priority;          /**< Load/execution priority: lower loads first (0-99 geometry, 100-199 visual, 200-299 behavior). */
    uint32_t api_version;       /**< TE_API_VERSION against which this plugin was compiled. */
} PluginMetadata;

/**
 * Pure C virtual method table representing the complete lifecycle interface of a plugin.
 * FROZEN ABI: The order and signature of these function pointers must never change.
 */
typedef struct PluginInterface {
    /**
     * Initialize plugin instance with host services and configuration.
     * Called once when the plugin DLL is loaded.
     *
     * @param ctx Pointer to the host-provided PluginContext.
     * @return TE_S_OK on success, or error HRESULT.
     */
    HRESULT (*Initialize)(const PluginContext* ctx);

    /**
     * Enable plugin operation and hook subscriptions.
     * Pre-allocate all working memory here to avoid per-frame allocations.
     *
     * @return TE_S_OK on success, or error HRESULT.
     */
    HRESULT (*Enable)(void);

    /**
     * Disable plugin operation, unsubscribe hooks, and suspend background activities.
     *
     * @return TE_S_OK on success, or error HRESULT.
     */
    HRESULT (*Disable)(void);

    /**
     * Periodic animation or logic update step called on the UI thread.
     *
     * @param delta_time Elapsed time since last update in seconds.
     * @return TE_S_OK on success, or error HRESULT.
     */
    HRESULT (*Update)(float delta_time);

    /**
     * Release all allocated resources and prepare for DLL unload.
     *
     * @return TE_S_OK on success, or error HRESULT.
     */
    HRESULT (*Shutdown)(void);

    /**
     * Retrieve static plugin metadata.
     *
     * @return Pointer to static PluginMetadata struct.
     */
    const PluginMetadata* (*GetMetadata)(void);

    /**
     * Retrieve configurable settings schema for GUI auto-generation.
     *
     * @return Pointer to static PluginSettings struct, or NULL if no settings.
     */
    const PluginSettings* (*GetSettings)(void);
} PluginInterface;

/**
 * Function pointer type matching the GetPluginInterface entry point
 * that every plugin DLL must export.
 */
typedef const PluginInterface* (*GetPluginInterfaceFunc)(void);

#ifdef __cplusplus
}
#endif
