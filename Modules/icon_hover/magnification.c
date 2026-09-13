#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#include "magnification.h"

float TE_MagnifyWeightGaussian(float u) {
    if (u <= 0.0f) {
        return 1.0f;
    }
    if (u > 1.0f) {
        return 0.0f;
    }
    // expf(-u^2 / (2 * sigma^2)) where sigma = 0.4
    // 2 * 0.4 * 0.4 = 0.32
    return expf(-(u * u) / 0.32f);
}

float TE_MagnifyWeightCubic(float u) {
    if (u <= 0.0f) {
        return 1.0f;
    }
    if (u >= 1.0f) {
        return 0.0f;
    }
    return (1.0f - u) * (1.0f - u) * (1.0f + 2.0f * u);
}

float TE_MagnifyWeightCosine(float u) {
    if (u <= 0.0f) {
        return 1.0f;
    }
    if (u >= 1.0f) {
        return 0.0f;
    }
    return (1.0f + cosf((float)M_PI * u)) / 2.0f;
}

float TE_MagnifyWeightLinear(float u) {
    if (u <= 0.0f) {
        return 1.0f;
    }
    if (u >= 1.0f) {
        return 0.0f;
    }
    return 1.0f - u;
}

void TE_MagnifyComputeScales(
    float cursor_x,
    const float* icon_centers_x,
    float* out_scales,
    int count,
    float radius,
    float max_scale,
    TE_MagnifyCurveType curve
) {
    int i;
    
    if (count <= 0) {
        return;
    }
    
    if (radius <= 0.0f) {
        for (i = 0; i < count; ++i) {
            out_scales[i] = 1.0f;
        }
        return;
    }
    
    if (max_scale < 1.0f) {
        max_scale = 1.0f;
    }
    
    for (i = 0; i < count; ++i) {
        float d = fabsf(cursor_x - icon_centers_x[i]);
        float u = d / radius;
        
        if (u >= 1.0f) {
            out_scales[i] = 1.0f;
        } else {
            float w = 0.0f;
            switch (curve) {
                case TE_CURVE_GAUSSIAN:
                    w = TE_MagnifyWeightGaussian(u);
                    break;
                case TE_CURVE_CUBIC:
                    w = TE_MagnifyWeightCubic(u);
                    break;
                case TE_CURVE_COSINE:
                    w = TE_MagnifyWeightCosine(u);
                    break;
                case TE_CURVE_LINEAR:
                    w = TE_MagnifyWeightLinear(u);
                    break;
            }
            out_scales[i] = 1.0f + (max_scale - 1.0f) * w;
        }
    }
}
