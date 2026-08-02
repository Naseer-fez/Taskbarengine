#include "icon_layout.h"

void TE_LayoutComputePositions(
    const float scales[], const float base_x[], 
    float out_x[], float out_y[], 
    int count, float icon_size, float taskbar_bottom_y)
{
    if (count <= 0) return;

    /* 1. Find pivot (max scale) */
    int pivot_idx = 0;
    float max_s = scales[0];
    for (int i = 1; i < count; i++) {
        if (scales[i] > max_s) {
            max_s = scales[i];
            pivot_idx = i;
        }
    }

    /* 2. Pivot stays in place */
    out_x[pivot_idx] = base_x[pivot_idx];
    out_y[pivot_idx] = taskbar_bottom_y - icon_size * scales[pivot_idx];

    /* 3. Left of pivot */
    float offset = 0.0f;
    for (int i = pivot_idx - 1; i >= 0; i--) {
        float shift_right = (scales[i + 1] - 1.0f) * icon_size * 0.5f;
        float shift_left  = (scales[i] - 1.0f) * icon_size * 0.5f;
        offset -= (shift_right + shift_left);
        out_x[i] = base_x[i] + offset;
        out_y[i] = taskbar_bottom_y - icon_size * scales[i];
    }

    /* 4. Right of pivot */
    offset = 0.0f;
    for (int i = pivot_idx + 1; i < count; i++) {
        float shift_left = (scales[i - 1] - 1.0f) * icon_size * 0.5f;
        float shift_right = (scales[i] - 1.0f) * icon_size * 0.5f;
        offset += (shift_left + shift_right);
        out_x[i] = base_x[i] + offset;
        out_y[i] = taskbar_bottom_y - icon_size * scales[i];
    }
}
