#pragma once

#include "te_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log level enumeration.
 */
typedef enum TE_LogLevel {
    TE_LOG_DEBUG = 0,
    TE_LOG_INFO  = 1,
    TE_LOG_WARN  = 2,
    TE_LOG_ERROR = 3
} TE_LogLevel;

/**
 * @brief Function pointer signature for logging.
 * @param level The log severity level.
 * @param fmt Standard printf format string.
 * @note Thread safety: Implementations must be thread-safe.
 */
typedef void (*LogFunc)(TE_LogLevel level, const char* fmt, ...);

/**
 * @brief Main engine log function.
 * @param level The log severity level.
 * @param fmt Standard printf format string.
 * @note Thread safety: Thread-safe. OutputDebugStringA is thread-safe.
 */
void TE_LogWrite(TE_LogLevel level, const char* fmt, ...);

#ifdef __cplusplus
}
#endif
