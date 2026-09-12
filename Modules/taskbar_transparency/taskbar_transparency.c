#include "taskbar_transparency.h"
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
    TE_TransparencyConfigParseJson(config, &s_plugin_state.settings);
    s_plugin_state.tray_state.apply_secondary = s_plugin_state.settings.apply_secondary;
    ACCENT_POLICY policy = TE_ComputeAccentPolicy(&s_plugin_state.settings);
    bool is_enabled = s_plugin_state.enabled;
    bool setting_enabled = s_plugin_state.settings.enabled;
    ReleaseSRWLockExclusive(&s_plugin_state.lock);

    if (is_enabled) {
        if (!setting_enabled) {
            TransparencyLog(TE_LOG_INFO, "Config disabled transparency: restoring native taskbar");
            TE_TrayRestoreAll(&s_plugin_state.tray_state);
        } else {
            TransparencyLog(TE_LOG_INFO, "Re-applying live transparency policy (mode=%s, opacity=%.2f)",
                           TE_TransparencyModeToString(s_plugin_state.settings.mode),
                           s_plugin_state.settings.opacity);
            TE_TrayDiscoverAll(&s_plugin_state.tray_state);
            TE_TrayApplyPolicyToAll(&s_plugin_state.tray_state, &policy);
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
    ReleaseSRWLockShared(&s_plugin_state.lock);

    TE_TrayHandleDisplayChange(&s_plugin_state.tray_state, &policy);
}

static void OnDisplayChanged(uint32_t type, const void* data, void* user_data) {
    (void)type;
    (void)data;
    (void)user_data;

    if (!s_plugin_state.enabled) return;

    TransparencyLog(TE_LOG_INFO, "Display topology change detected; re-applying policy to taskbars");

    AcquireSRWLockShared(&s_plugin_state.lock);
    ACCENT_POLICY policy = TE_ComputeAccentPolicy(&s_plugin_state.settings);
    ReleaseSRWLockShared(&s_plugin_state.lock);

    TE_TrayHandleDisplayChange(&s_plugin_state.tray_state, &policy);
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
        { .enum_val = { 0, s_mode_options, 5 } }
    },
    {
        "opacity",
        "Opacity",
        "Visual opacity level from 0.0 (fully clear) to 1.0 (fully opaque)",
        TE_SETTING_FLOAT,
        { .float_val = { 0.0f, 0.0f, 1.0f, 0.05f } }
    },
    {
        "color",
        "Tint Color",
        "32-bit ARGB/ABGR tint color for acrylic or tint mode",
        TE_SETTING_COLOR,
        { .color_val = { 0x00000000 } }
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
    TE_TrayApplyPolicyToAll(&s_plugin_state.tray_state, &policy);
    s_plugin_state.enabled = true;
    ReleaseSRWLockExclusive(&s_plugin_state.lock);

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
    TE_TrayRestoreAll(&s_plugin_state.tray_state);
    s_plugin_state.enabled = false;
    ReleaseSRWLockExclusive(&s_plugin_state.lock);

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
