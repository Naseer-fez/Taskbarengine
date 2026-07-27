#include "sdk/te_log.h"
#include "sdk/te_log_impl.h"
#include <stdarg.h>

void TE_LogWrite(TE_LogLevel level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    TE_LogWriteV(level, fmt, args);
    va_end(args);
}
