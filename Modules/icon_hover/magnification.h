#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define TE_MAX_TASKBAR_ICONS 64

typedef enum {
    TE_CURVE_GAUSSIAN = 0,
    TE_CURVE_COSINE,
    TE_CURVE_LINEAR,
    TE_CURVE_CUBIC
} TE_MagnifyCurveType;

/**
 * @brief Compute the magnification scale for a single icon.
 * @param distance Distance from cursor to icon center.
 * @param radius Magnification effect radius.
 * @param max_scale Maximum scale at center.
 * @param curve Curve type.
 * @return Scale factor (>= 1.0f).
 */
float TE_MagnifyScale(float distance, float radius, float max_scale, TE_MagnifyCurveType curve);

/**
 * @brief Batch compute magnification scales for an array of icons.
 * @param cursor_x X-coordinate of cursor.
 * @param icon_centers Array of original icon center X positions.
 * @param out_scales Output array of scale factors.
 * @param count Number of icons.
 * @param radius Magnification effect radius.
 * @param max_scale Maximum scale at center.
 * @param curve Curve type.
 */
void TE_MagnifyComputeScales(float cursor_x, const float icon_centers[], float out_scales[], int count, float radius, float max_scale, TE_MagnifyCurveType curve);

#ifdef __cplusplus
}
#endif
