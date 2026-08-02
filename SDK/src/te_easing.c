#include <sdk/te_easing.h>
#include <math.h>

float TE_EasingApply(float t, TE_EasingType type)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    switch (type) {
        case TE_EASE_LINEAR:
            return t;
        case TE_EASE_IN_QUAD:
            return t * t;
        case TE_EASE_OUT_QUAD:
            return t * (2.0f - t);
        case TE_EASE_IN_OUT_QUAD:
            return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        case TE_EASE_IN_CUBIC:
            return t * t * t;
        case TE_EASE_OUT_CUBIC: {
            float u = t - 1.0f;
            return u * u * u + 1.0f;
        }
        case TE_EASE_IN_OUT_CUBIC:
            return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
        case TE_EASE_OUT_EXPO:
            return 1.0f - powf(2.0f, -10.0f * t);
        default:
            return t;
    }
}

float TE_Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float TE_LerpEased(float a, float b, float t, TE_EasingType type)
{
    return TE_Lerp(a, b, TE_EasingApply(t, type));
}
