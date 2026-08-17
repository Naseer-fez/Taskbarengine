#pragma once

#include "te_types.h"
#include "te_version.h"
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
 *
 * Returned by GetMetadata(). The engine reads this to determine plugin
 * identity, priority ordering, and build compatibility.
 */
typedef struct PluginMetadata {
    const char* name;
    const char* version;
    const char* author;
    const char* description;
    uint32_t priority;
    TE_PluginCompatibility compatibility;  /**< Windows build range (v2). Zero = no restriction. */
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

/* --------------------------------------------------------------------------
 * Callback function pointer types
 * -------------------------------------------------------------------------- */

typedef HRESULT (*EventCallbackFunc)(uint32_t event_type, const void* event_data, void* user_data);
typedef HRESULT (*SubscribeFunc)(uint32_t event_type, EventCallbackFunc callback, void* user_data);
typedef HRESULT (*UnsubscribeFunc)(uint32_t event_type, EventCallbackFunc callback);
typedef void (*RequestRedrawFunc)(void);
typedef HRESULT (*PublishStateFunc)(const char* key, const StateValue* val);
typedef HRESULT (*QueryStateFunc)(const char* key, StateValue* out_val);

/* v2 function pointer types */

/**
 * @brief Timer callback function signature.
 * @param user_data Opaque context passed during registration.
 * @note Always invoked on the UI thread (Shell_TrayWnd message pump).
 */
typedef void (*TE_TimerCallback)(void* user_data);

/**
 * @brief Subscribe to a specific Win32 window message on Shell_TrayWnd.
 *
 * When subscribed, the subclass proc will dispatch events for this
 * message type to the plugin's event subscribers.
 *
 * @param win_msg Win32 message ID (e.g. WM_MOUSEMOVE).
 * @return S_OK on success, E_OUTOFMEMORY if table is full.
 */
typedef HRESULT (*SubscribeToMessageFunc)(UINT win_msg);

/**
 * @brief Unsubscribe from a previously subscribed Win32 window message.
 *
 * @param win_msg Win32 message ID to unsubscribe from.
 * @return S_OK on success, S_FALSE if not found.
 */
typedef HRESULT (*UnsubscribeFromMessageFunc)(UINT win_msg);

/**
 * @brief Register a timer that fires on the UI thread.
 *
 * @param interval_ms  Timer interval in milliseconds.
 * @param recurring    TRUE for repeating timer, FALSE for one-shot.
 * @param callback     Function to invoke when the timer fires.
 * @param user_data    Opaque context passed to the callback.
 * @return S_OK on success.
 */
typedef HRESULT (*RegisterTimerFunc)(uint32_t interval_ms, BOOL recurring,
                                     TE_TimerCallback callback, void* user_data);

/**
 * @brief Cancel a previously registered timer by callback pointer.
 *
 * All timers registered with the given callback are cancelled.
 *
 * @param callback The callback used during registration.
 * @return S_OK if found and cancelled, S_FALSE if not found.
 */
typedef HRESULT (*CancelTimerFunc)(TE_TimerCallback callback);

/* --------------------------------------------------------------------------
 * Plugin Context (Core → Plugin)
 * -------------------------------------------------------------------------- */

/**
 * @brief Plugin Context passed from Engine to Plugin upon initialization.
 *
 * ## ABI Compatibility
 *
 * - `struct_size` is always the first field and indicates how many bytes
 *   of this struct the engine actually populated.
 * - `api_version` is the second field and indicates the ABI generation.
 * - Plugins compiled against v1 will see `struct_size` equal to the
 *   offset of the v2 fields. They must not access any field beyond
 *   their known struct size.
 * - v2 plugins should check both `api_version >= 2` AND
 *   `struct_size >= offsetof(PluginContext, <last_v2_field>) + sizeof(<last_v2_field>)`
 *   before accessing v2 fields.
 *
 * @note Thread safety: Provided read-only to plugins. Pointer remains
 *       valid for the lifetime of the plugin (Initialize → Shutdown).
 */
typedef struct PluginContext {
    /* --- ABI envelope (always present) --- */
    uint32_t struct_size;           /**< sizeof(PluginContext) as set by the engine */
    uint32_t api_version;           /**< TE_API_VERSION at build time of the engine */

    /* --- v1 fields (frozen) --- */
    HWND taskbar_hwnd;              /**< Primary Shell_TrayWnd handle */
    HMONITOR monitor;               /**< Target monitor handle */
    uint32_t dpi;                   /**< Target monitor DPI */
    const cJSON* config;            /**< Plugin's configuration sub-tree */
    LogFunc log;                    /**< Thread-safe logger */
    SubscribeFunc subscribe;        /**< Event subscription */
    UnsubscribeFunc unsubscribe;    /**< Event unsubscription */
    RequestRedrawFunc request_redraw; /**< Requests redraw/commit */
    PublishStateFunc publish_state;  /**< Inter-plugin state sharing (publish) */
    QueryStateFunc query_state;      /**< Inter-plugin state sharing (query) */
    void* core_opaque;              /**< Core internal context pointer */

    /* --- v2 fields (appended) --- */
    SubscribeToMessageFunc subscribe_message;       /**< Win32 message subscription */
    UnsubscribeFromMessageFunc unsubscribe_message;  /**< Win32 message unsubscription */
    RegisterTimerFunc register_timer;                /**< UI-thread timer registration */
    CancelTimerFunc cancel_timer;                    /**< Timer cancellation */
} PluginContext;

/**
 * @brief Helper macro: check if a v2 PluginContext field is available.
 *
 * Usage:
 *   if (TE_CTX_HAS_FIELD(ctx, register_timer)) {
 *       ctx->register_timer(100, FALSE, my_cb, NULL);
 *   }
 */
#define TE_CTX_HAS_FIELD(ctx, field) \
    ((ctx) && \
     (ctx)->api_version >= 2 && \
     (ctx)->struct_size >= (uint32_t)(offsetof(PluginContext, field) + sizeof((ctx)->field)))

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
