#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Types of easing functions available for animation.
 */
typedef enum {
    TE_EASE_LINEAR,
    TE_EASE_IN_QUAD,
    TE_EASE_OUT_QUAD,
    TE_EASE_IN_OUT_QUAD,
    TE_EASE_IN_CUBIC,
    TE_EASE_OUT_CUBIC,
    TE_EASE_IN_OUT_CUBIC,
    TE_EASE_OUT_EXPO
} TE_EasingType;

/**
 * @brief Apply an easing function to a normalized time parameter.
 * @param t Normalized time in [0.0, 1.0]. Input is clamped to this range.
 * @param type The easing curve to apply.
 * @return The eased value in [0.0, 1.0].
 */
float TE_EasingApply(float t, TE_EasingType type);

/**
 * @brief Linearly interpolate between two values.
 * @param a Start value.
 * @param b End value.
 * @param t Interpolant (typically [0.0, 1.0]).
 * @return Interpolated value.
 */
float TE_Lerp(float a, float b, float t);

/**
 * @brief Interpolate between two values using an easing curve.
 * @param a Start value.
 * @param b End value.
 * @param t Normalized time in [0.0, 1.0].
 * @param type The easing curve to apply to t before interpolation.
 * @return Interpolated value.
 */
float TE_LerpEased(float a, float b, float t, TE_EasingType type);

#ifdef __cplusplus
}
#endif
