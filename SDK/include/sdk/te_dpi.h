#pragma once

#include "te_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Scale an integer value from 96 DPI (100%) to target DPI with proper rounding.
 * @param value Base pixel value at 96 DPI.
 * @param dpi Target DPI.
 * @return Scaled pixel value.
 */
int TE_ScaleDPI(int value, uint32_t dpi);

#ifdef __cplusplus
}
#endif
