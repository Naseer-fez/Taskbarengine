#include "taskbar_transparency.h"
#include "xaml_taskbar_brush.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static TE_TransparencyPluginState s_plugin_state;

static void TransparencyLog(TE_LogLevel level, const char* fmt, ...) {
    if (s_plugin_state.ctx && s_plugin_state.ctx->log) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        s_plugin_state.ctx->log(level, "TaskbarTransparency", buf);
    }
}

const TE_TransparencyPluginState* TE_TransparencyGetState(void) {
    return &s_plugin_state;
}

static void LogPolicyApplication(const char* reason, const ACCENT_POLICY* policy) {
    if (!policy) return;
    uint32_t applied = 0;
    uint32_t bridge_applied = 0;
    uint32_t primary_count = 0;
    bool primary_applied = false;
    bool primary_bridge_applied = false;
    bool primary_bridge_found = false;
    for (uint32_t i = 0; i < s_plugin_state.tray_state.count; ++i) {
        const TE_TrackedTaskbar* taskbar = &s_plugin_state.tray_state.taskbars[i];
        if (taskbar->is_primary) {
            primary_count++;
            primary_applied = taskbar->policy_applied;
            primary_bridge_applied = taskbar->composition_policy_applied;
            primary_bridge_found = taskbar->composition_hwnd != NULL;
        }
        if (taskbar->policy_applied) applied++;
        if (taskbar->composition_policy_applied) bridge_applied++;
    }
    TransparencyLog(primary_applied ? TE_LOG_INFO : TE_LOG_WARNING,
        "%s accent policy API acceptance (visual_pixels=unverified): state=%u flags=%u tint=0x%08X root=%u/%u bridge=%u/%u primary_root=%s primary_bridge=%s API=%s",
        reason ? reason : "Taskbar",
        (unsigned)policy->AccentState,
        policy->AccentFlags,
        policy->GradientColor,
        applied,
        s_plugin_state.tray_state.count,
        bridge_applied,
        s_plugin_state.tray_state.count,
        primary_count ? (primary_applied ? "yes" : "no") : "not-found",
        primary_count && primary_bridge_found ? (primary_bridge_applied ? "yes" : "no") : "not-found",
        TE_CompositionIsAvailable() ? "available" : "missing");
    for (uint32_t i = 0; i < s_plugin_state.tray_state.count; ++i) {
        const TE_TrackedTaskbar* taskbar = &s_plugin_state.tray_state.taskbars[i];
        wchar_t root_class[128] = L"?";
        wchar_t bridge_class[128] = L"?";
        DWORD root_pid = 0, bridge_pid = 0;
        const DWORD root_tid = taskbar->hwnd ? GetWindowThreadProcessId(taskbar->hwnd, &root_pid) : 0;
        const DWORD bridge_tid = taskbar->composition_hwnd ? GetWindowThreadProcessId(taskbar->composition_hwnd, &bridge_pid) : 0;
        if (taskbar->hwnd && IsWindow(taskbar->hwnd)) GetClassNameW(taskbar->hwnd, root_class, 128);
        if (taskbar->composition_hwnd && IsWindow(taskbar->composition_hwnd)) GetClassNameW(taskbar->composition_hwnd, bridge_class, 128);
        char root_utf8[128] = "?", bridge_utf8[128] = "?";
        WideCharToMultiByte(CP_UTF8, 0, root_class, -1, root_utf8, sizeof(root_utf8), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, bridge_class, -1, bridge_utf8, sizeof(bridge_utf8), NULL, NULL);
        TransparencyLog(taskbar->policy_applied ? TE_LOG_INFO : TE_LOG_WARNING,
            "SetWindowCompositionAttribute(root) HWND=%p class=%s pid=%lu tid=%lu api_accepted=%s error=%lu visual_pixels=unverified",
            (void*)taskbar->hwnd, root_utf8, root_pid, root_tid,
            taskbar->policy_applied ? "yes" : "no", taskbar->policy_last_error);
        TransparencyLog(taskbar->composition_policy_applied ? TE_LOG_INFO : TE_LOG_WARNING,
            "SetWindowCompositionAttribute(bridge) HWND=%p class=%s pid=%lu tid=%lu api_accepted=%s error=%lu visual_pixels=unverified",
            (void*)taskbar->composition_hwnd, bridge_utf8, bridge_pid, bridge_tid,
            taskbar->composition_policy_applied ? "yes" : "no", taskbar->composition_last_error);
    }
}

/* ── Event Callbacks ────────────────────────────────────────────────── */

static void OnConfigChanged(uint32_t type, const void* data, void* user_data) {
    (void)type;
    (void)user_data;

    if (!data) return;
    const TE_ConfigChangedData* changed = (const TE_ConfigChangedData*)data;

    if (changed->plugin_name && strcmp(changed->plugin_name, "taskbar_transparency") != 0) {
        return;
    }

    const struct cJSON* config = (const struct cJSON*)changed->new_config;
    if (!config) return;

    TransparencyLog(TE_LOG_INFO, "Hot-reload config change received");

    AcquireSRWLockExclusive(&s_plugin_state.lock);
    const bool had_secondary = s_plugin_state.settings.apply_secondary;
    TE_TrackedTaskbar previous_taskbars[TE_MAX_TRACKED_TASKBARS];
    const uint32_t previous_count = s_plugin_state.tray_state.count;
    memcpy(previous_taskbars, s_plugin_state.tray_state.taskbars,
           previous_count * sizeof(previous_taskbars[0]));
    TE_TransparencyConfigParseJson(config, &s_plugin_state.settings);
    s_plugin_state.tray_state.apply_secondary = s_plugin_state.settings.apply_secondary;
    ACCENT_POLICY policy = TE_ComputeAccentPolicy(&s_plugin_state.settings);
    bool is_enabled = s_plugin_state.enabled;
    bool setting_enabled = s_plugin_state.settings.enabled;
    const bool apply_secondary = s_plugin_state.settings.apply_secondary;
    TE_TransparencySettings settings = s_plugin_state.settings;
    ReleaseSRWLockExclusive(&s_plugin_state.lock);

    if (is_enabled) {
        if (!setting_enabled) {
            TransparencyLog(TE_LOG_INFO, "Config disabled transparency: restoring native taskbar appearance");
            TE_TrayRestoreAll(&s_plugin_state.tray_state);
            if (TE_XamlTaskbarIsSupported()) TE_XamlTaskbarSetAppearance(&settings);
        } else {
            if (had_secondary && !apply_secondary) {
                for (uint32_t i = 0; i < previous_count; ++i) {
                    if (!previous_taskbars[i].is_primary && previous_taskbars[i].hwnd &&
                        IsWindow(previous_taskbars[i].hwnd)) {
                        TE_RestoreNativeAccent(previous_taskbars[i].hwnd);
                        if (previous_taskbars[i].composition_hwnd && IsWindow(previous_taskbars[i].composition_hwnd)) {
                            TE_RestoreNativeAccent(previous_taskbars[i].composition_hwnd);
                        }
                    }
                }
            }
            TransparencyLog(TE_LOG_INFO, "Re-applying live transparency (mode=%s, opacity=%.2f)",
                           TE_TransparencyModeToString(settings.mode), settings.opacity);
            TE_TrayDiscoverAll(&s_plugin_state.tray_state);
            TE_TrayApplyPolicyToAll(&s_plugin_state.tray_state, &policy);
            LogPolicyApplication("Hot reload", &policy);
            if (!TE_TrayWatchCompositionBridge(&s_plugin_state.tray_state, true)) {
                TransparencyLog(TE_LOG_WARNING, "SetWinEventHook failed after config reload (GetLastError=%lu)", GetLastError());
            }
            if (TE_XamlTaskbarIsSupported()) {
                TE_XamlTaskbarSetAppearance(&settings);
                TE_XamlTaskbarStart();
            }
        }
    }
}

static void OnDpiChanged(uint32_t type, const void* data, void* user_data) {
    (void)type;
    (void)data;
    (void)user_data;

    if (!s_plugin_state.enabled) return;

    TransparencyLog(TE_LOG_INFO, "DPI change detected; re-applying policy to taskbars");

    AcquireSRWLockShared(&s_plugin_state.lock);
    ACCENT_POLICY policy = TE_ComputeAccentPolicy(&s_plugin_state.settings);
    TE_TransparencySettings settings = s_plugin_state.settings;
    ReleaseSRWLockShared(&s_plugin_state.lock);

    if (TE_XamlTaskbarIsSupported()) {
        TE_XamlTaskbarSetAppearance(&settings);
    } else {
        TE_TrayHandleDisplayChange(&s_plugin_state.tray_state, &policy);
        LogPolicyApplication("DPI change", &policy);
        if (!s_plugin_state.tray_state.debounce_timer_id) {
            TransparencyLog(TE_LOG_WARNING, "SetTimer(rediscovery) failed after DPI change (GetLastError=%lu)", GetLastError());
        }
    }
}

static void OnDisplayChanged(uint32_t type, const void* data, void* user_data) {
    (void)type;
    (void)data;
    (void)user_data;

    if (!s_plugin_state.enabled) return;

    TransparencyLog(TE_LOG_INFO, "Display topology change detected; re-applying policy to taskbars");

    AcquireSRWLockShared(&s_plugin_state.lock);
    ACCENT_POLICY policy = TE_ComputeAccentPolicy(&s_plugin_state.settings);
    TE_TransparencySettings settings = s_plugin_state.settings;
    ReleaseSRWLockShared(&s_plugin_state.lock);

    if (TE_XamlTaskbarIsSupported()) {
        TE_XamlTaskbarSetAppearance(&settings);
    } else {
        TE_TrayHandleDisplayChange(&s_plugin_state.tray_state, &policy);
        LogPolicyApplication("Display change", &policy);
        if (!s_plugin_state.tray_state.debounce_timer_id) {
            TransparencyLog(TE_LOG_WARNING, "SetTimer(rediscovery) failed after display change (GetLastError=%lu)", GetLastError());
        }
    }
}

/* ── Settings Descriptors for Dynamic GUI Generation ─────────────────── */

static const char* s_mode_options[] = {
    "clear",
    "blur",
    "acrylic",
    "tint",
    "disabled"
};

static const SettingDescriptor s_descriptors[] = {
    {
        "enabled",
        "Enable Transparency",
        "Master toggle for taskbar visual composition effects",
        TE_SETTING_BOOL,
        { .bool_val = { 1 } }
    },
    {
        "mode",
        "Visual Mode",
        "Visual transparency mode (clear, blur, acrylic, tint, disabled)",
        TE_SETTING_ENUM,
        { .enum_val = { 2, s_mode_options, 5 } }
    },
    {
        "opacity",
        "Opacity",
        "Visual opacity level from 0.0 (fully clear) to 1.0 (fully opaque)",
        TE_SETTING_FLOAT,
        { .float_val = { 0.65f, 0.0f, 1.0f, 0.05f } }
    },
    {
        "color",
        "Tint Color",
        "32-bit ARGB tint color for acrylic or tint mode (noise and luminosity blend are controlled by Windows)",
        TE_SETTING_COLOR,
        { .color_val = { 0x44000000 } }
    },
    {
        "accent_flags",
        "Accent Flags",
        "DWM accent policy flags (default 2 for acrylic luminance)",
        TE_SETTING_INT,
        { .int_val = { 2, 0, 255, 1 } }
    },
    {
        "apply_secondary",
        "Apply to Secondary Monitors",
        "Whether to apply transparency to taskbars on multi-monitor setups",
        TE_SETTING_BOOL,
        { .bool_val = { 1 } }
    }
};

static const PluginSettings s_plugin_settings = {
    s_descriptors,
    sizeof(s_descriptors) / sizeof(s_descriptors[0])
};

static const PluginMetadata s_plugin_metadata = {
    "taskbar_transparency",
    "Taskbar Transparency",
    "Native taskbar transparency, acrylic blur, and custom opacity controls across Windows 10 and 11.",
    "TaskbarEngine",
    100, /* 1.0.0 */
    100, /* Priority 100: visual layer */
    TE_API_VERSION
};

/* ── Plugin Interface Lifecycle Methods ─────────────────────────────── */

static HRESULT Initialize(const PluginContext* ctx) {
    memset(&s_plugin_state, 0, sizeof(s_plugin_state));
    InitializeSRWLock(&s_plugin_state.lock);

    TE_TransparencyConfigSetDefaults(&s_plugin_state.settings);
    TE_TrayDiscoveryInit(&s_plugin_state.tray_state);

    if (!TE_CompositionApiInit()) {
        /* User32 resolution failed or unsupported platform */
        TransparencyLog(TE_LOG_WARNING, "SetWindowCompositionAttribute could not be resolved from user32.dll");
    }

    s_plugin_state.ctx = ctx;

    if (ctx && ctx->config) {
        TE_TransparencyConfigParseJson(ctx->config, &s_plugin_state.settings);
    }

    s_plugin_state.tray_state.apply_secondary = s_plugin_state.settings.apply_secondary;
    s_plugin_state.initialized = true;

    TransparencyLog(TE_LOG_INFO, "Initialized taskbar_transparency plugin (mode=%s, opacity=%.2f)",
                   TE_TransparencyModeToString(s_plugin_state.settings.mode),
                   s_plugin_state.settings.opacity);

    return TE_S_OK;
}

static HRESULT Enable(void) {
    if (!s_plugin_state.initialized) {
        return TE_E_FAIL;
    }

    TransparencyLog(TE_LOG_INFO, "Enabling taskbar_transparency");

    if (s_plugin_state.ctx && s_plugin_state.ctx->subscribe) {
        s_plugin_state.ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged, NULL);
        s_plugin_state.ctx->subscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged, NULL);
        s_plugin_state.ctx->subscribe(TE_EVENT_DISPLAY_CHANGED, OnDisplayChanged, NULL);
    }

    AcquireSRWLockExclusive(&s_plugin_state.lock);
    s_plugin_state.tray_state.apply_secondary = s_plugin_state.settings.apply_secondary;
    TE_TrayDiscoverAll(&s_plugin_state.tray_state);
    ACCENT_POLICY policy = TE_ComputeAccentPolicy(&s_plugin_state.settings);
    const TE_TransparencySettings settings = s_plugin_state.settings;
    const bool use_xaml = TE_XamlTaskbarIsSupported();
    TE_TrayApplyPolicyToAll(&s_plugin_state.tray_state, &policy);
    LogPolicyApplication("Enable", &policy);
    if (!TE_TrayWatchCompositionBridge(&s_plugin_state.tray_state, true)) {
        TransparencyLog(TE_LOG_WARNING, "SetWinEventHook(EVENT_OBJECT_CREATE..DESTROY) failed; bridge recreation will be handled on display/DPI/config events only (GetLastError=%lu)", GetLastError());
    }
    s_plugin_state.enabled = true;
    ReleaseSRWLockExclusive(&s_plugin_state.lock);

    if (use_xaml) {
        TransparencyLog(TE_LOG_INFO, "Windows build supports XAML taskbar; using Explorer XAML background brush path");
        TE_XamlTaskbarSetAppearance(&settings);
        TE_XamlTaskbarStart();
    }

    return TE_S_OK;
}

static HRESULT Disable(void) {
    if (!s_plugin_state.enabled) {
        return TE_S_OK;
    }

    TransparencyLog(TE_LOG_INFO, "Disabling taskbar_transparency: reverting shell styling");

    if (s_plugin_state.ctx && s_plugin_state.ctx->unsubscribe) {
        s_plugin_state.ctx->unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged);
        s_plugin_state.ctx->unsubscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged);
        s_plugin_state.ctx->unsubscribe(TE_EVENT_DISPLAY_CHANGED, OnDisplayChanged);
    }

    AcquireSRWLockExclusive(&s_plugin_state.lock);
    const bool use_xaml = TE_XamlTaskbarIsSupported();
    TE_TrayWatchCompositionBridge(&s_plugin_state.tray_state, false);
    TE_TrayRestoreAll(&s_plugin_state.tray_state);
    s_plugin_state.enabled = false;
    TE_TransparencySettings settings = s_plugin_state.settings;
    settings.enabled = false;
    ReleaseSRWLockExclusive(&s_plugin_state.lock);

    if (use_xaml) TE_XamlTaskbarSetAppearance(&settings);

    return TE_S_OK;
}

static HRESULT Update(float delta_time) {
    (void)delta_time;
    /* Zero CPU idle: event-driven architecture requires no per-frame work */
    return TE_S_OK;
}

static HRESULT Shutdown(void) {
    TransparencyLog(TE_LOG_INFO, "Shutting down taskbar_transparency plugin");

    if (s_plugin_state.enabled) {
        Disable();
    }

    AcquireSRWLockExclusive(&s_plugin_state.lock);
    TE_TrayCleanup(&s_plugin_state.tray_state);
    TE_CompositionApiShutdown();
    s_plugin_state.initialized = false;
    s_plugin_state.ctx = NULL;
    ReleaseSRWLockExclusive(&s_plugin_state.lock);

    return TE_S_OK;
}

static const PluginMetadata* GetMetadata(void) {
    return &s_plugin_metadata;
}

static const PluginSettings* GetSettings(void) {
    return &s_plugin_settings;
}

static const PluginInterface s_interface = {
    Initialize,
    Enable,
    Disable,
    Update,
    Shutdown,
    GetMetadata,
    GetSettings
};

const PluginInterface* TE_TransparencyGetPluginInterface(void) {
    return &s_interface;
}

#ifndef TE_STATIC_LINK
TE_EXPORT const PluginInterface* GetPluginInterface(void) {
    return TE_TransparencyGetPluginInterface();
}
#endif
