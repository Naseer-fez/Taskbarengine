#ifndef TE_TASKBAR_RESIZE_H
#define TE_TASKBAR_RESIZE_H

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <sdk/te_dpi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_DEFAULT_TASKBAR_HEIGHT 48

typedef struct {
    int height;
    int padding_top;
    int padding_bottom;
    int icon_spacing;
} TE_ResizeConfig;

/**
 * @brief Calculate the vertical offset to center child elements within a compact taskbar.
 * 
 * In Windows 11, XAML layout inside DesktopWindowContentBridge assumes the default 48px bar
 * with fixed internal padding. To center the content in a reduced height, we shift the child
 * window vertically by the symmetric difference between target and default height, plus any
 * custom padding offset.
 *
 * @param new_height      Target taskbar height in logical pixels.
 * @param default_height  Default taskbar height in logical pixels (48).
 * @param padding_top     Custom top padding adjustment in logical pixels.
 * @param padding_bottom  Custom bottom padding adjustment in logical pixels.
 * @param dpi             Current monitor DPI (96 = 100%, 120 = 125%, 144 = 150%).
 * @return                Vertical offset in physical pixels (typically <= 0).
 */
int TE_CalculateCenteringOffset(int new_height, int default_height, int padding_top, int padding_bottom, uint32_t dpi);

/**
 * @brief Calculate the updated desktop work area for a given monitor and compact taskbar height.
 *
 * @param monitor_rect    The full monitor bounding rectangle.
 * @param taskbar_height  The compact taskbar height in logical pixels.
 * @param dpi             Current monitor DPI.
 * @param out_work_area   Pointer to RECT receiving the computed work area.
 * @return                TRUE on success, FALSE on invalid argument.
 */
BOOL TE_CalculateWorkArea(const RECT* monitor_rect, int taskbar_height, uint32_t dpi, RECT* out_work_area);

#ifdef __cplusplus
}
#endif

#endif /* TE_TASKBAR_RESIZE_H */
