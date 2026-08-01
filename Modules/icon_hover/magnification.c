#include "magnification.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

float TE_MagnifyScale(float distance, float radius, float max_scale, TE_MagnifyCurveType curve)
{
    if (radius <= 0.0f) return 1.0f;
    if (max_scale <= 1.0f) return 1.0f;

    float abs_d = fabsf(distance);

    switch (curve) {
        case TE_CURVE_GAUSSIAN: {
            /* sigma = radius / 3.0f so at distance = radius, scale is ~1.0001 (decayed) */
            float sigma = radius / 3.0f;
            float exponent = -(abs_d * abs_d) / (2.0f * sigma * sigma);
            float factor = expf(exponent);
            return 1.0f + (max_scale - 1.0f) * factor;
        }

        case TE_CURVE_COSINE: {
            if (abs_d >= radius) return 1.0f;
            float norm = abs_d / radius;
            float factor = 0.5f * (1.0f + cosf(M_PI * norm));
            return 1.0f + (max_scale - 1.0f) * factor;
        }

        case TE_CURVE_LINEAR: {
            if (abs_d >= radius) return 1.0f;
            float factor = 1.0f - (abs_d / radius);
            return 1.0f + (max_scale - 1.0f) * factor;
        }

        case TE_CURVE_CUBIC: {
            if (abs_d >= radius) return 1.0f;
            float norm = abs_d / radius;
            float term = 1.0f - (norm * norm);
            float factor = term * term; /* Smooth step cubic / quintic falloff */
            return 1.0f + (max_scale - 1.0f) * factor;
        }

        default:
            return 1.0f;
    }
}

void TE_MagnifyComputeScales(float cursor_x, const float icon_centers[], float out_scales[], int count, float radius, float max_scale, TE_MagnifyCurveType curve)
{
    if (!icon_centers || !out_scales || count <= 0) return;

    for (int i = 0; i < count; i++) {
        float dist = icon_centers[i] - cursor_x;
        out_scales[i] = TE_MagnifyScale(dist, radius, max_scale, curve);
    }
}
