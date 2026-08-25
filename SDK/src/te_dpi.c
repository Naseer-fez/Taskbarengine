#include <sdk/te_dpi.h>

int TE_ScaleDPI(int value, UINT dpi)
{
    if (dpi == 0) {
        return 0;
    }
    return MulDiv(value, (int)dpi, 96);
}

int TE_UnscaleDPI(int value, UINT dpi)
{
    if (dpi == 0) {
        return 0;
    }
    return MulDiv(value, 96, (int)dpi);
}
