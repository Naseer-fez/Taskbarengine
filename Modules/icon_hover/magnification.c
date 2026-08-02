#include "magnification.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float TE_MagnifyScale(float distance, float radius, float max_scale, TE_MagnifyCurveType curve)
{
    if (radius <= 0.0f || max_scale <= 1.0f) {
        return 1.0f;
    }

    float abs_dist = fabsf(distance);
    if (abs_dist >= radius) {
        return 1.0f;
    }

    float t = abs_dist / radius;
    float influence = 0.0f;

    switch (curve) {
        case TE_CURVE_LINEAR:
            influence = 1.0f - t;
            break;
        case TE_CURVE_CUBIC:
            influence = (1.0f - t) * (1.0f - t) * (1.0f - t);
            break;
        case TE_CURVE_COSINE:
            influence = (cosf(t * (float)M_PI) + 1.0f) * 0.5f;
            break;
        case TE_CURVE_GAUSSIAN:
            // Standard gaussian falloff approx
            influence = expf(-4.6f * t * t);
            break;
        default:
            influence = 1.0f - t;
            break;
    }

    return 1.0f + (max_scale - 1.0f) * influence;
}

void TE_MagnifyComputeScales(float cursor_x, const float icon_centers[], float out_scales[], int count, float radius, float max_scale, TE_MagnifyCurveType curve)
{
    for (int i = 0; i < count; i++) {
        out_scales[i] = TE_MagnifyScale(icon_centers[i] - cursor_x, radius, max_scale, curve);
    }
}
