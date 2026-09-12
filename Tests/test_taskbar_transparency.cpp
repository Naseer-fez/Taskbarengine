#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "composition_api.h"
#include "tray_discovery.h"
#include "transparency_config.h"
#include "taskbar_transparency.h"
#include <sdk/te_plugin.h>
#include <sdk/te_events.h>
#include <cJSON.h>

using Catch::Approx;

/* ── 1. Byte Packing and Channel Positioning ──────────────────────── */

TEST_CASE("Taskbar Transparency - ABGR Byte Packing", "[plugin][transparency][packing]") {
    SECTION("Standard Channel Positioning") {
        uint8_t a = 0xAA;
        uint8_t r = 0x11;
        uint8_t g = 0x22;
        uint8_t b = 0x33;

        uint32_t packed = TE_PackABGR(a, r, g, b);

        // ABGR memory layout: 0xAABBGGRR
        REQUIRE(((packed >> 24) & 0xFF) == a);
        REQUIRE(((packed >> 16) & 0xFF) == b);
        REQUIRE(((packed >> 8) & 0xFF) == g);
        REQUIRE((packed & 0xFF) == r);
        REQUIRE(packed == 0xAA332211);
    }

    SECTION("Boundary Values") {
        REQUIRE(TE_PackABGR(0, 0, 0, 0) == 0x00000000);
        REQUIRE(TE_PackABGR(0xFF, 0xFF, 0xFF, 0xFF) == 0xFFFFFFFF);
        REQUIRE(TE_PackABGR(0x12, 0x34, 0x56, 0x78) == 0x12785634);
    }
}

/* ── 2. ARGB to ABGR Color Conversion and Opacity ──────────────────── */

TEST_CASE("Taskbar Transparency - ARGB to ABGR Conversion", "[plugin][transparency][color]") {
    SECTION("Red and Blue Channel Swap with Full Opacity") {
        // Pure Red: 0xFFFF0000 (A=FF, R=FF, G=00, B=00)
        // Expect ABGR: 0xFF0000FF (A=FF, B=00, G=00, R=FF)
        uint32_t abgr_red = TE_ArgbToAbgr(0xFFFF0000, 1.0f);
        REQUIRE(abgr_red == 0xFF0000FF);

        // Pure Blue: 0xFF0000FF (A=FF, R=00, G=00, B=FF)
        // Expect ABGR: 0xFFFF0000 (A=FF, B=FF, G=00, R=00)
        uint32_t abgr_blue = TE_ArgbToAbgr(0xFF0000FF, 1.0f);
        REQUIRE(abgr_blue == 0xFFFF0000);

        // Pure Green: 0xFF00FF00 (A=FF, R=00, G=FF, B=00)
        // Expect ABGR: 0xFF00FF00 (A=FF, B=00, G=FF, R=00)
        uint32_t abgr_green = TE_ArgbToAbgr(0xFF00FF00, 1.0f);
        REQUIRE(abgr_green == 0xFF00FF00);
    }

    SECTION("Zero Raw Alpha Expansion") {
        // When raw alpha is 0 (e.g. 0x00112233 from 6-digit hex #112233),
        // opacity scales full base alpha (255)
        uint32_t full_op = TE_ArgbToAbgr(0x00112233, 1.0f);
        REQUIRE(((full_op >> 24) & 0xFF) == 255);
        REQUIRE(((full_op >> 16) & 0xFF) == 0x33);
        REQUIRE(((full_op >> 8) & 0xFF) == 0x22);
        REQUIRE((full_op & 0xFF) == 0x11);

        uint32_t half_op = TE_ArgbToAbgr(0x00112233, 0.5f);
        REQUIRE(((half_op >> 24) & 0xFF) == 127);
    }

    SECTION("Non-Zero Raw Alpha Scaling") {
        // When raw alpha is explicitly set (e.g. 0x80112233, alpha=128),
        // opacity scales that alpha value
        uint32_t half_op = TE_ArgbToAbgr(0x80112233, 0.5f);
        REQUIRE(((half_op >> 24) & 0xFF) == 64);

        uint32_t full_op = TE_ArgbToAbgr(0x80112233, 1.0f);
        REQUIRE(((full_op >> 24) & 0xFF) == 128);
    }

    SECTION("Opacity Clamping") {
        uint32_t clamped_low = TE_ArgbToAbgr(0xFF112233, -0.5f);
        REQUIRE(((clamped_low >> 24) & 0xFF) == 0);

        uint32_t clamped_high = TE_ArgbToAbgr(0xFF112233, 2.0f);
        REQUIRE(((clamped_high >> 24) & 0xFF) == 255);
    }
}

/* ── 3. Accent Policy Computation ──────────────────────────────────── */

TEST_CASE("Taskbar Transparency - Accent Policy Computation", "[plugin][transparency][policy]") {
    TE_TransparencySettings settings;
    TE_TransparencyConfigSetDefaults(&settings);

    SECTION("Clear / Transparent Mode") {
        settings.enabled = true;
        settings.mode = TE_MODE_CLEAR;
        settings.accent_flags = 2;

        ACCENT_POLICY policy = TE_ComputeAccentPolicy(&settings);
        REQUIRE(policy.AccentState == ACCENT_ENABLE_TRANSPARENTGRADIENT);
        REQUIRE(policy.AccentFlags == 2);
        REQUIRE(policy.GradientColor == 0);
        REQUIRE(policy.AnimationId == 0);
    }

    SECTION("Blur Behind Mode") {
        settings.enabled = true;
        settings.mode = TE_MODE_BLUR;
        settings.accent_flags = 0;

        ACCENT_POLICY policy = TE_ComputeAccentPolicy(&settings);
        REQUIRE(policy.AccentState == ACCENT_ENABLE_BLURBEHIND);
        REQUIRE(policy.AccentFlags == 0);
        REQUIRE(policy.GradientColor == 0);
    }

    SECTION("Acrylic Mode with Alpha Clamping >= 1") {
        settings.enabled = true;
        settings.mode = TE_MODE_ACRYLIC;
        settings.color_argb = 0xFF112233;
        settings.accent_flags = 2;
        settings.opacity = 0.5f;

        ACCENT_POLICY policy = TE_ComputeAccentPolicy(&settings);
        REQUIRE(policy.AccentState == ACCENT_ENABLE_ACRYLICBLURBEHIND);
        REQUIRE(policy.AccentFlags == 2);
        REQUIRE(((policy.GradientColor >> 24) & 0xFF) == 127);

        // Win11 requirement: zero opacity must clamp alpha >= 1 to prevent black background glitch
        settings.opacity = 0.0f;
        ACCENT_POLICY policy_zero = TE_ComputeAccentPolicy(&settings);
        REQUIRE(policy_zero.AccentState == ACCENT_ENABLE_ACRYLICBLURBEHIND);
        REQUIRE(((policy_zero.GradientColor >> 24) & 0xFF) >= 1);
    }

    SECTION("Tint Mode - Semi-Transparent vs Opaque") {
        settings.enabled = true;
        settings.mode = TE_MODE_TINT;
        settings.color_argb = 0x00AABBCC;
        settings.accent_flags = 2;

        // Semi-transparent -> ACCENT_ENABLE_TRANSPARENTGRADIENT
        settings.opacity = 0.6f;
        ACCENT_POLICY policy_semi = TE_ComputeAccentPolicy(&settings);
        REQUIRE(policy_semi.AccentState == ACCENT_ENABLE_TRANSPARENTGRADIENT);
        REQUIRE(policy_semi.AccentFlags == 2);

        // Opaque (opacity >= 1.0f) -> ACCENT_ENABLE_GRADIENT
        settings.opacity = 1.0f;
        ACCENT_POLICY policy_opaque = TE_ComputeAccentPolicy(&settings);
        REQUIRE(policy_opaque.AccentState == ACCENT_ENABLE_GRADIENT);
        REQUIRE(policy_opaque.AccentFlags == 2);
    }

    SECTION("Disabled Mode / Master Toggle False") {
        settings.enabled = false;
        settings.mode = TE_MODE_ACRYLIC;

        ACCENT_POLICY policy = TE_ComputeAccentPolicy(&settings);
        REQUIRE(policy.AccentState == ACCENT_DISABLED);
        REQUIRE(policy.GradientColor == 0);

        settings.enabled = true;
        settings.mode = TE_MODE_DISABLED;
        ACCENT_POLICY policy_disabled = TE_ComputeAccentPolicy(&settings);
        REQUIRE(policy_disabled.AccentState == ACCENT_DISABLED);
        REQUIRE(policy_disabled.GradientColor == 0);
    }

    SECTION("NULL Settings Safe Fallback") {
        ACCENT_POLICY policy = TE_ComputeAccentPolicy(NULL);
        REQUIRE(policy.AccentState == ACCENT_DISABLED);
        REQUIRE(policy.GradientColor == 0);
    }
}

/* ── 4. Configuration Parsing and Mode Conversions ─────────────────── */

TEST_CASE("Taskbar Transparency - Mode String Conversions", "[plugin][transparency][modes]") {
    REQUIRE(TE_TransparencyModeFromString("clear") == TE_MODE_CLEAR);
    REQUIRE(TE_TransparencyModeFromString("transparent") == TE_MODE_CLEAR);
    REQUIRE(TE_TransparencyModeFromString("CLEAR") == TE_MODE_CLEAR);
    REQUIRE(TE_TransparencyModeFromString("TRANSPARENT") == TE_MODE_CLEAR);

    REQUIRE(TE_TransparencyModeFromString("blur") == TE_MODE_BLUR);
    REQUIRE(TE_TransparencyModeFromString("BLUR") == TE_MODE_BLUR);

    REQUIRE(TE_TransparencyModeFromString("acrylic") == TE_MODE_ACRYLIC);
    REQUIRE(TE_TransparencyModeFromString("ACRYLIC") == TE_MODE_ACRYLIC);

    REQUIRE(TE_TransparencyModeFromString("tint") == TE_MODE_TINT);
    REQUIRE(TE_TransparencyModeFromString("opaque") == TE_MODE_TINT);

    REQUIRE(TE_TransparencyModeFromString("disabled") == TE_MODE_DISABLED);
    REQUIRE(TE_TransparencyModeFromString("native") == TE_MODE_DISABLED);

    REQUIRE(TE_TransparencyModeFromString("unknown_value") == TE_MODE_CLEAR);
    REQUIRE(TE_TransparencyModeFromString(NULL) == TE_MODE_CLEAR);

    REQUIRE(std::string(TE_TransparencyModeToString(TE_MODE_CLEAR)) == "clear");
    REQUIRE(std::string(TE_TransparencyModeToString(TE_MODE_BLUR)) == "blur");
    REQUIRE(std::string(TE_TransparencyModeToString(TE_MODE_ACRYLIC)) == "acrylic");
    REQUIRE(std::string(TE_TransparencyModeToString(TE_MODE_TINT)) == "tint");
    REQUIRE(std::string(TE_TransparencyModeToString(TE_MODE_DISABLED)) == "disabled");
}

TEST_CASE("Taskbar Transparency - JSON Configuration Parsing", "[plugin][transparency][config]") {
    TE_TransparencySettings settings;

    SECTION("Full Valid Configuration JSON") {
        const char* json =
            "{\n"
            "    \"enabled\": true,\n"
            "    \"mode\": \"acrylic\",\n"
            "    \"opacity\": 0.75,\n"
            "    \"color\": \"#112233\",\n"
            "    \"accent_flags\": 3,\n"
            "    \"apply_secondary\": false\n"
            "}";

        REQUIRE(TE_TransparencyConfigParse(json, &settings) == true);
        REQUIRE(settings.enabled == true);
        REQUIRE(settings.mode == TE_MODE_ACRYLIC);
        REQUIRE(settings.opacity == Approx(0.75f));
        REQUIRE(settings.color_argb == 0x00112233);
        REQUIRE(settings.accent_flags == 3);
        REQUIRE(settings.apply_secondary == false);
    }

    SECTION("Hex String Parsing Variants") {
        const char* json1 = "{\"color\": \"#80112233\"}";
        REQUIRE(TE_TransparencyConfigParse(json1, &settings) == true);
        const char* json2 = "{\"color\": \"0x80112233\"}"; // 0x prefix
        REQUIRE(TE_TransparencyConfigParse(json2, &settings) == true);
        REQUIRE(settings.color_argb == 0x80112233);

        const char* json3 = "{\"color\": \"0x112233\"}";
        REQUIRE(TE_TransparencyConfigParse(json3, &settings) == true);
        REQUIRE(settings.color_argb == 0x00112233);

        const char* json_int = "{\"color\": 1122867}"; // Integer 0x112233
        REQUIRE(TE_TransparencyConfigParse(json_int, &settings) == true);
        REQUIRE(settings.color_argb == 1122867);
    }

    SECTION("Missing Fields Fallback to Defaults") {
        const char* json = "{\"opacity\": 0.4}";
        REQUIRE(TE_TransparencyConfigParse(json, &settings) == true);
        REQUIRE(settings.enabled == true);
        REQUIRE(settings.mode == TE_MODE_CLEAR);
        REQUIRE(settings.opacity == Approx(0.4f));
        REQUIRE(settings.color_argb == 0);
        REQUIRE(settings.accent_flags == 2);
        REQUIRE(settings.apply_secondary == true);
    }

    SECTION("JSONC Comments Support") {
        const char* jsonc =
            "// Taskbar transparency config\n"
            "{\n"
            "    /* Mode selection */\n"
            "    \"mode\": \"blur\",\n"
            "    \"opacity\": 0.2 // subtle blur\n"
            "}";
        REQUIRE(TE_TransparencyConfigParse(jsonc, &settings) == true);
        REQUIRE(settings.mode == TE_MODE_BLUR);
        REQUIRE(settings.opacity == Approx(0.2f));
    }

    SECTION("Wrapped in taskbar_transparency Object") {
        const char* wrapped =
            "{\n"
            "    \"taskbar_transparency\": {\n"
            "        \"enabled\": true,\n"
            "        \"mode\": \"tint\",\n"
            "        \"opacity\": 0.9\n"
            "    }\n"
            "}";
        REQUIRE(TE_TransparencyConfigParse(wrapped, &settings) == true);
        REQUIRE(settings.mode == TE_MODE_TINT);
        REQUIRE(settings.opacity == Approx(0.9f));
    }

    SECTION("Invalid JSON Handling") {
        REQUIRE(TE_TransparencyConfigParse(NULL, &settings) == false);
        REQUIRE(TE_TransparencyConfigParse("{ invalid json ", &settings) == false);
    }
}

/* ── 5. Multi-Tray Tracking List Logic ─────────────────────────────── */

TEST_CASE("Taskbar Transparency - Multi-Tray Tracking List", "[plugin][transparency][tray]") {
    TE_TrayDiscoveryState state;
    TE_TrayDiscoveryInit(&state);

    REQUIRE(state.count == 0);
    REQUIRE(state.apply_secondary == true);

    SECTION("Adding Primary and Secondary Taskbars") {
        HWND primary_hwnd = (HWND)(uintptr_t)0x10001;
        HWND sec_hwnd1 = (HWND)(uintptr_t)0x20001;
        HWND sec_hwnd2 = (HWND)(uintptr_t)0x20002;

        REQUIRE(TE_TrayDiscoveryAddTracked(&state, primary_hwnd, true) == true);
        REQUIRE(state.count == 1);
        REQUIRE(state.taskbars[0].hwnd == primary_hwnd);
        REQUIRE(state.taskbars[0].is_primary == true);

        REQUIRE(TE_TrayDiscoveryAddTracked(&state, sec_hwnd1, false) == true);
        REQUIRE(state.count == 2);
        REQUIRE(state.taskbars[1].hwnd == sec_hwnd1);
        REQUIRE(state.taskbars[1].is_primary == false);

        REQUIRE(TE_TrayDiscoveryAddTracked(&state, sec_hwnd2, false) == true);
        REQUIRE(state.count == 3);
    }

    SECTION("Updating Existing Taskbar Window") {
        HWND hwnd = (HWND)(uintptr_t)0x50001;
        REQUIRE(TE_TrayDiscoveryAddTracked(&state, hwnd, false) == true);
        REQUIRE(state.count == 1);
        REQUIRE(state.taskbars[0].is_primary == false);

        // Update same HWND to primary
        REQUIRE(TE_TrayDiscoveryAddTracked(&state, hwnd, true) == true);
        REQUIRE(state.count == 1);
        REQUIRE(state.taskbars[0].is_primary == true);
    }

    SECTION("Fixed Capacity Limit (TE_MAX_TRACKED_TASKBARS)") {
        for (uint32_t i = 0; i < TE_MAX_TRACKED_TASKBARS; i++) {
            HWND fake_hwnd = (HWND)(uintptr_t)(0x10000 + i);
            REQUIRE(TE_TrayDiscoveryAddTracked(&state, fake_hwnd, i == 0) == true);
        }
        REQUIRE(state.count == TE_MAX_TRACKED_TASKBARS);

        // Attempting to add one more beyond limit must return false without buffer overflow
        HWND overflow_hwnd = (HWND)(uintptr_t)0x99999;
        REQUIRE(TE_TrayDiscoveryAddTracked(&state, overflow_hwnd, false) == false);
        REQUIRE(state.count == TE_MAX_TRACKED_TASKBARS);
    }

    SECTION("Cleanup Resets State") {
        TE_TrayDiscoveryAddTracked(&state, (HWND)(uintptr_t)0x111, true);
        REQUIRE(state.count == 1);

        TE_TrayCleanup(&state);
        REQUIRE(state.count == 0);
    }
}

/* ── 6. Dynamic API Resolution and Mock Function Injection ─────────── */

static BOOL WINAPI MockSetWindowCompositionAttribute(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA* pAttrData) {
    (void)hWnd;
    if (!pAttrData) return FALSE;
    if (pAttrData->Attrib != WCA_ACCENT_POLICY) return FALSE;
    if (pAttrData->cbData != sizeof(ACCENT_POLICY)) return FALSE;
    return TRUE;
}

TEST_CASE("Taskbar Transparency - Dynamic API and Mock Injection", "[plugin][transparency][mock]") {
    SECTION("Inject Mock Composition Function") {
        TE_CompositionSetApiFunction(MockSetWindowCompositionAttribute);
        REQUIRE(TE_CompositionGetApiFunction() == MockSetWindowCompositionAttribute);
        REQUIRE(TE_CompositionIsAvailable() == true);

        // Reset to default
        TE_CompositionSetApiFunction(nullptr);
        REQUIRE(TE_CompositionGetApiFunction() == nullptr);
    }
}

/* ── 7. Plugin Interface Lifecycle and Metadata ────────────────────── */

TEST_CASE("Taskbar Transparency - Plugin Lifecycle Interface", "[plugin][transparency][lifecycle]") {
    const PluginInterface* plugin = TE_TransparencyGetPluginInterface();
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->Initialize != nullptr);
    REQUIRE(plugin->Enable != nullptr);
    REQUIRE(plugin->Disable != nullptr);
    REQUIRE(plugin->Update != nullptr);
    REQUIRE(plugin->Shutdown != nullptr);
    REQUIRE(plugin->GetMetadata != nullptr);
    REQUIRE(plugin->GetSettings != nullptr);

    SECTION("Metadata Verification") {
        const PluginMetadata* meta = plugin->GetMetadata();
        REQUIRE(meta != nullptr);
        REQUIRE(std::string(meta->name) == "taskbar_transparency");
        REQUIRE(std::string(meta->display_name) == "Taskbar Transparency");
        REQUIRE(meta->version == 100);
        REQUIRE(meta->priority == 100); // Visual layer
        REQUIRE(meta->api_version == TE_API_VERSION);
    }

    SECTION("Settings Descriptors Verification") {
        const PluginSettings* settings_desc = plugin->GetSettings();
        REQUIRE(settings_desc != nullptr);
        REQUIRE(settings_desc->count >= 5);
        REQUIRE(std::string(settings_desc->descriptors[0].key) == "enabled");
        REQUIRE(std::string(settings_desc->descriptors[1].key) == "mode");
    }

    SECTION("Lifecycle Execution (Zero CPU Idle)") {
        PluginContext ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.struct_size = sizeof(ctx);
        ctx.api_version = TE_API_VERSION;

        REQUIRE(plugin->Initialize(&ctx) == TE_S_OK);
        REQUIRE(plugin->Enable() == TE_S_OK);

        // Update must return S_OK immediately without spinning (zero CPU idle)
        REQUIRE(plugin->Update(0.016f) == TE_S_OK);
        REQUIRE(plugin->Update(0.016f) == TE_S_OK);

        REQUIRE(plugin->Disable() == TE_S_OK);
        REQUIRE(plugin->Shutdown() == TE_S_OK);
    }
}
