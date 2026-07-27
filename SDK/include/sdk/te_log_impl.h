#pragma once

#include "te_log.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TE_LOG_RING_SIZE 256
#define TE_LOG_MESSAGE_SIZE 248

typedef struct TE_LogEntry {
    uint32_t level;
    uint32_t timestamp_ms;
    char message[TE_LOG_MESSAGE_SIZE];
} TE_LogEntry;

/**
 * @brief Initialize the ring buffer logging system and start the flush thread.
 * @param log_dir Directory where logs will be stored (or NULL to auto-resolve %LOCALAPPDATA%\TaskbarEngine\logs).
 * @param min_level Minimum log level to record.
 * @param to_file Whether to write log entries to disk.
 * @return S_OK on success.
 */
HRESULT TE_LogInit(const wchar_t* log_dir, TE_LogLevel min_level, bool to_file);

/**
 * @brief Flush and shutdown the ring buffer logger.
 */
void TE_LogShutdown(void);

/**
 * @brief Format and push log entry into ring buffer.
 */
void TE_LogWriteV(TE_LogLevel level, const char* fmt, va_list args);

#ifdef __cplusplus
}
#endif
