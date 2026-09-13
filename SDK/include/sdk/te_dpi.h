#pragma once
#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Scale a pixel value from 96-DPI baseline to the given DPI.
 *
 * @param value   The pixel value at 96 DPI.
 * @param dpi     Target DPI (e.g., 96, 120, 144, 192).
 * @return        Scaled pixel value. Returns 0 if dpi is 0.
 *
 * @note Thread Safety: Thread-safe (pure function, no shared state).
 */
int TE_ScaleDPI(int value, UINT dpi);

/**
 * Unscale a pixel value from the given DPI back to 96-DPI baseline.
 *
 * @param value   The pixel value at the given DPI.
 * @param dpi     Source DPI.
 * @return        Unscaled pixel value at 96 DPI. Returns 0 if dpi is 0.
 *
 * @note Thread Safety: Thread-safe (pure function, no shared state).
 */
int TE_UnscaleDPI(int value, UINT dpi);

#ifdef __cplusplus
}
#endif
