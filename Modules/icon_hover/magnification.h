#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TE_MagnifyCurveType {
    TE_CURVE_GAUSSIAN = 0,
    TE_CURVE_COSINE   = 1,
    TE_CURVE_LINEAR   = 2,
    TE_CURVE_CUBIC    = 3
} TE_MagnifyCurveType;

/**
 * @brief Calculate magnification scale for a single icon at distance 'd' from cursor.
 */
float TE_MagnifyScale(float distance, float radius, float max_scale, TE_MagnifyCurveType curve);

/**
 * @brief Batch calculate scale factors for array of icon centers.
 */
void TE_MagnifyComputeScales(float cursor_x, const float icon_centers[], float out_scales[], int count, float radius, float max_scale, TE_MagnifyCurveType curve);

#ifdef __cplusplus
}
#endif
