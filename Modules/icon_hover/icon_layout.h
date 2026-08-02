#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute displaced X-positions for magnified icons.
 * Icons spread apart proportionally to their scale increase.
 * @param scales       Array of scale factors [count].
 * @param base_x       Array of original icon center X positions [count].
 * @param out_x        Output array of displaced X positions [count].
 * @param out_y        Output array of displaced Y positions [count].
 * @param count        Number of icons.
 * @param icon_size    Base icon size in pixels (used for displacement calculation).
 * @param taskbar_bottom_y  Y-coordinate of taskbar bottom edge (icons grow upward from here).
 */
void TE_LayoutComputePositions(
    const float scales[], const float base_x[], 
    float out_x[], float out_y[], 
    int count, float icon_size, float taskbar_bottom_y);

#ifdef __cplusplus
}
#endif
