#include "composition_api.h"

static pfnSetWindowCompositionAttribute s_pfnSetWindowCompositionAttribute = NULL;
static bool s_custom_function_injected = false;

bool TE_CompositionApiInit(void) {
    if (s_custom_function_injected && s_pfnSetWindowCompositionAttribute) {
        return true;
    }

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) {
        hUser32 = LoadLibraryW(L"user32.dll");
    }
    if (!hUser32) {
        return false;
    }

    s_pfnSetWindowCompositionAttribute = (pfnSetWindowCompositionAttribute)(void*)GetProcAddress(
        hUser32,
        "SetWindowCompositionAttribute"
    );

    return (s_pfnSetWindowCompositionAttribute != NULL);
}

void TE_CompositionApiShutdown(void) {
    if (!s_custom_function_injected) {
        s_pfnSetWindowCompositionAttribute = NULL;
    }
}

bool TE_CompositionIsAvailable(void) {
    if (s_pfnSetWindowCompositionAttribute) {
        return true;
    }
    return TE_CompositionApiInit();
}

void TE_CompositionSetApiFunction(pfnSetWindowCompositionAttribute fn) {
    s_pfnSetWindowCompositionAttribute = fn;
    s_custom_function_injected = (fn != NULL);
}

pfnSetWindowCompositionAttribute TE_CompositionGetApiFunction(void) {
    return s_pfnSetWindowCompositionAttribute;
}

uint32_t TE_PackABGR(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return (((uint32_t)a << 24) |
            ((uint32_t)b << 16) |
            ((uint32_t)g << 8)  |
            ((uint32_t)r));
}

uint32_t TE_ArgbToAbgr(uint32_t argb, float opacity) {
    float clamped_op = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    uint8_t a_raw = (uint8_t)((argb >> 24) & 0xFF);
    uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((argb >> 8) & 0xFF);
    uint8_t b = (uint8_t)(argb & 0xFF);

    uint8_t a;
    if (a_raw == 0) {
        a = (uint8_t)(255.0f * clamped_op);
    } else {
        a = (uint8_t)((float)a_raw * clamped_op);
    }

    return TE_PackABGR(a, r, g, b);
}

ACCENT_POLICY TE_ComputeAccentPolicy(const TE_TransparencySettings* settings) {
    ACCENT_POLICY policy = { ACCENT_DISABLED, 0, 0, 0 };
    if (!settings || !settings->enabled) {
        return policy;
    }

    switch (settings->mode) {
        case TE_MODE_CLEAR:
            policy.AccentState = ACCENT_ENABLE_TRANSPARENTGRADIENT;
            policy.AccentFlags = (settings->accent_flags != 0) ? settings->accent_flags : 2;
            policy.GradientColor = 0;
            policy.AnimationId = 0;
            break;

        case TE_MODE_BLUR:
            policy.AccentState = ACCENT_ENABLE_BLURBEHIND;
            policy.AccentFlags = settings->accent_flags;
            policy.GradientColor = 0;
            policy.AnimationId = 0;
            break;

        case TE_MODE_ACRYLIC: {
            policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
            policy.AccentFlags = (settings->accent_flags != 0) ? settings->accent_flags : 2;
            uint32_t abgr = TE_ArgbToAbgr(settings->color_argb, settings->opacity);
            uint8_t alpha = (uint8_t)((abgr >> 24) & 0xFF);
            if (alpha < 1) {
                alpha = 1; /* Win11 requires non-zero alpha to avoid black background glitch */
            }
            policy.GradientColor = (abgr & 0x00FFFFFF) | ((uint32_t)alpha << 24);
            policy.AnimationId = 0;
            break;
        }

        case TE_MODE_TINT: {
            if (settings->opacity >= 1.0f) {
                policy.AccentState = ACCENT_ENABLE_GRADIENT;
            } else {
                policy.AccentState = ACCENT_ENABLE_TRANSPARENTGRADIENT;
            }
            policy.AccentFlags = (settings->accent_flags != 0) ? settings->accent_flags : 2;
            policy.GradientColor = TE_ArgbToAbgr(settings->color_argb, settings->opacity);
            policy.AnimationId = 0;
            break;
        }

        case TE_MODE_DISABLED:
        default:
            policy.AccentState = ACCENT_DISABLED;
            policy.AccentFlags = 0;
            policy.GradientColor = 0;
            policy.AnimationId = 0;
            break;
    }

    return policy;
}

bool TE_ApplyAccentPolicy(HWND hwnd, const ACCENT_POLICY* policy) {
    if (!hwnd || !IsWindow(hwnd) || !policy) {
        return false;
    }

    if (!s_pfnSetWindowCompositionAttribute) {
        if (!TE_CompositionApiInit()) {
            return false;
        }
    }

    WINDOWCOMPOSITIONATTRIBDATA data;
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = (void*)policy;
    data.cbData = sizeof(ACCENT_POLICY);

    BOOL res = s_pfnSetWindowCompositionAttribute(hwnd, &data);
    return (res != FALSE);
}

bool TE_RestoreNativeAccent(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }

    ACCENT_POLICY policy = { ACCENT_DISABLED, 0, 0, 0 };
    WINDOWCOMPOSITIONATTRIBDATA data;
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &policy;
    data.cbData = sizeof(policy);

    if (s_pfnSetWindowCompositionAttribute || TE_CompositionApiInit()) {
        s_pfnSetWindowCompositionAttribute(hwnd, &data);
    }

    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    RedrawWindow(hwnd, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    return true;
}
