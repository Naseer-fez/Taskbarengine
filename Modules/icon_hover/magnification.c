#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#include <emmintrin.h>
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
    
    float scale_range = max_scale - 1.0f;
    float inv_radius = 1.0f / radius;
    i = 0;

    switch (curve) {
        case TE_CURVE_GAUSSIAN: {
            __m128 v_cursor = _mm_set1_ps(cursor_x);
            __m128 v_inv_rad = _mm_set1_ps(inv_radius);
            __m128 v_one = _mm_set1_ps(1.0f);
            __m128 v_zero = _mm_setzero_ps();
            __m128 v_range = _mm_set1_ps(scale_range);
            __m128 v_inv_sig = _mm_set1_ps(3.125f); /* 1.0f / 0.32f */
            __m128 v_c1 = _mm_set1_ps(0.5f);
            __m128 v_c2 = _mm_set1_ps(0.1f);
            __m128 v_c3 = _mm_set1_ps(0.0083333333f);
            __m128 v_sign_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));

            for (; i + 3 < count; i += 4) {
                __m128 v_centers = _mm_loadu_ps(&icon_centers_x[i]);
                __m128 v_diff = _mm_sub_ps(v_cursor, v_centers);
                __m128 v_d = _mm_and_ps(v_diff, v_sign_mask);
                __m128 v_u = _mm_mul_ps(v_d, v_inv_rad);
                __m128 v_mask = _mm_cmplt_ps(v_u, v_one);

                /* Clamp u in [0, 1] */
                v_u = _mm_min_ps(_mm_max_ps(v_u, v_zero), v_one);
                __m128 v_u2 = _mm_mul_ps(v_u, v_u);
                __m128 v_x = _mm_mul_ps(v_u2, v_inv_sig);

                /* Pade [3/3] rational approximation for exp(-x) */
                __m128 v_x2 = _mm_mul_ps(v_x, v_x);
                __m128 v_x3 = _mm_mul_ps(v_x2, v_x);
                __m128 v_t1 = _mm_mul_ps(v_c1, v_x);
                __m128 v_t2 = _mm_mul_ps(v_c2, v_x2);
                __m128 v_t3 = _mm_mul_ps(v_c3, v_x3);

                __m128 v_num = _mm_add_ps(_mm_sub_ps(_mm_sub_ps(v_one, v_t1), v_t3), v_t2);
                __m128 v_den = _mm_add_ps(_mm_add_ps(_mm_add_ps(v_one, v_t1), v_t2), v_t3);
                __m128 v_w = _mm_div_ps(v_num, v_den);
                v_w = _mm_and_ps(v_w, v_mask);

                __m128 v_scale = _mm_add_ps(v_one, _mm_mul_ps(v_range, v_w));
                _mm_storeu_ps(&out_scales[i], v_scale);
            }
            for (; i < count; ++i) {
                float d = fabsf(cursor_x - icon_centers_x[i]);
                float u = d * inv_radius;
                out_scales[i] = (u >= 1.0f) ? 1.0f : (1.0f + scale_range * TE_MagnifyWeightGaussian(u));
            }
            break;
        }
        case TE_CURVE_CUBIC: {
            __m128 v_cursor = _mm_set1_ps(cursor_x);
            __m128 v_inv_rad = _mm_set1_ps(inv_radius);
            __m128 v_one = _mm_set1_ps(1.0f);
            __m128 v_two = _mm_set1_ps(2.0f);
            __m128 v_zero = _mm_setzero_ps();
            __m128 v_range = _mm_set1_ps(scale_range);
            __m128 v_sign_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));

            for (; i + 3 < count; i += 4) {
                __m128 v_centers = _mm_loadu_ps(&icon_centers_x[i]);
                __m128 v_diff = _mm_sub_ps(v_cursor, v_centers);
                __m128 v_d = _mm_and_ps(v_diff, v_sign_mask);
                __m128 v_u = _mm_mul_ps(v_d, v_inv_rad);
                __m128 v_mask = _mm_cmplt_ps(v_u, v_one);

                v_u = _mm_min_ps(_mm_max_ps(v_u, v_zero), v_one);
                __m128 v_1_minus_u = _mm_sub_ps(v_one, v_u);
                __m128 v_1_plus_2u = _mm_add_ps(v_one, _mm_mul_ps(v_two, v_u));
                __m128 v_w = _mm_mul_ps(_mm_mul_ps(v_1_minus_u, v_1_minus_u), v_1_plus_2u);
                v_w = _mm_and_ps(v_w, v_mask);

                __m128 v_scale = _mm_add_ps(v_one, _mm_mul_ps(v_range, v_w));
                _mm_storeu_ps(&out_scales[i], v_scale);
            }
            for (; i < count; ++i) {
                float d = fabsf(cursor_x - icon_centers_x[i]);
                float u = d * inv_radius;
                out_scales[i] = (u >= 1.0f) ? 1.0f : (1.0f + scale_range * TE_MagnifyWeightCubic(u));
            }
            break;
        }
        case TE_CURVE_LINEAR: {
            __m128 v_cursor = _mm_set1_ps(cursor_x);
            __m128 v_inv_rad = _mm_set1_ps(inv_radius);
            __m128 v_one = _mm_set1_ps(1.0f);
            __m128 v_zero = _mm_setzero_ps();
            __m128 v_range = _mm_set1_ps(scale_range);
            __m128 v_sign_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));

            for (; i + 3 < count; i += 4) {
                __m128 v_centers = _mm_loadu_ps(&icon_centers_x[i]);
                __m128 v_diff = _mm_sub_ps(v_cursor, v_centers);
                __m128 v_d = _mm_and_ps(v_diff, v_sign_mask);
                __m128 v_u = _mm_mul_ps(v_d, v_inv_rad);
                __m128 v_mask = _mm_cmplt_ps(v_u, v_one);

                v_u = _mm_min_ps(_mm_max_ps(v_u, v_zero), v_one);
                __m128 v_w = _mm_sub_ps(v_one, v_u);
                v_w = _mm_and_ps(v_w, v_mask);

                __m128 v_scale = _mm_add_ps(v_one, _mm_mul_ps(v_range, v_w));
                _mm_storeu_ps(&out_scales[i], v_scale);
            }
            for (; i < count; ++i) {
                float d = fabsf(cursor_x - icon_centers_x[i]);
                float u = d * inv_radius;
                out_scales[i] = (u >= 1.0f) ? 1.0f : (1.0f + scale_range * TE_MagnifyWeightLinear(u));
            }
            break;
        }
        case TE_CURVE_COSINE:
        default: {
            for (i = 0; i < count; ++i) {
                float d = fabsf(cursor_x - icon_centers_x[i]);
                float u = d * inv_radius;
                out_scales[i] = (u >= 1.0f) ? 1.0f : (1.0f + scale_range * TE_MagnifyWeightCosine(u));
            }
            break;
        }
    }
}
