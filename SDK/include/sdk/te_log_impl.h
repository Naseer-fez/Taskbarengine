#pragma once
#include <sdk/te_types.h>
#include <sdk/te_log.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the ring buffer logging subsystem.
 * Creates the background flush thread and opens the log file.
 *
 * @param log_dir   Directory path for log files. Must not be NULL.
 * @param min_level Minimum severity level to record.
 * @return TE_S_OK on success, TE_E_INVALIDARG or TE_E_FAIL on error.
 *
 * @note Thread Safety: Call once during engine startup, before other threads.
 */
HRESULT TE_LogInit(const wchar_t* log_dir, TE_LogLevel min_level);

/**
 * Shut down the logging subsystem.
 * Flushes remaining entries, stops the flush thread, and closes the log file.
 *
 * @note Thread Safety: Call once during engine shutdown, after other threads stopped.
 */
void TE_LogShutdown(void);

/**
 * Force an immediate flush of all pending ring buffer entries to the log file.
 *
 * @note Thread Safety: Safe to call from any thread.
 */
void TE_LogFlush(void);

#ifdef __cplusplus
}
#endif
