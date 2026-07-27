#include "sdk/te_dpi.h"

int TE_ScaleDPI(int value, uint32_t dpi)
{
    if (value == 0) return 0;
    int64_t scaled = (int64_t)value * (int64_t)dpi;
    if (scaled >= 0) {
        return (int)((scaled + 48) / 96);
    } else {
        return (int)((scaled - 48) / 96);
    }
}
