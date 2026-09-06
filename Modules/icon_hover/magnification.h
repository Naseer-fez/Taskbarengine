#ifndef TE_MAGNIFICATION_H
#define TE_MAGNIFICATION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TE_MagnifyCurveType {
    TE_CURVE_GAUSSIAN = 0,
    TE_CURVE_CUBIC    = 1,
    TE_CURVE_COSINE   = 2,
    TE_CURVE_LINEAR   = 3
} TE_MagnifyCurveType;

// Individual weight functions.
// Input: u = normalized distance in [0, 1] where 0 = cursor position, 1 = edge of radius
// Output: weight in [0, 1] where 1 = full magnification, 0 = no magnification
float TE_MagnifyWeightGaussian(float u);  // exp(-u^2 / (2 * sigma^2)), sigma = 0.4
float TE_MagnifyWeightCubic(float u);     // (1 - u)^2 * (1 + 2*u)  — Hermite smoothstep
float TE_MagnifyWeightCosine(float u);    // (1 + cos(pi * u)) / 2
float TE_MagnifyWeightLinear(float u);    // 1 - u

// Batch scale computation for N icons.
// For each icon i:
//   d = |cursor_x - icon_centers_x[i]|
//   u = d / radius  (clamped: if u >= 1, scale = 1.0)
//   scale = 1.0 + (max_scale - 1.0) * weight(u)
// Guarantees: 1.0 <= out_scales[i] <= max_scale for all i
void TE_MagnifyComputeScales(
    float cursor_x,
    const float* icon_centers_x,
    float* out_scales,
    int count,
    float radius,
    float max_scale,
    TE_MagnifyCurveType curve
);

#ifdef __cplusplus
}
#endif

#endif /* TE_MAGNIFICATION_H */
