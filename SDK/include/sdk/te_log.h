#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Severity levels for TaskbarEngine log entries.
 */
typedef enum TE_LogLevel {
    TE_LOG_DEBUG = 0,   /**< Fine-grained diagnostic messages; typically compiled out in Release builds. */
    TE_LOG_INFO = 1,    /**< General informational events about normal operation. */
    TE_LOG_WARNING = 2, /**< Non-fatal issues or unexpected conditions that were handled. */
    TE_LOG_ERROR = 3    /**< Severe failures or unrecoverable error conditions. */
} TE_LogLevel;

/**
 * Function pointer callback type for receiving log messages from the logging subsystem.
 *
 * @param level Severity level of the message.
 * @param module Identifying string for the emitting component or plugin.
 * @param message The log message content.
 */
typedef void (*TE_LogFunc)(TE_LogLevel level, const char* module, const char* message);

/**
 * Submit a log message to the active TaskbarEngine logging system.
 * The message will be written to the active ring buffer / log sink and flushed asynchronously.
 *
 * @param level Severity level of the log entry.
 * @param module Non-null identifier for the emitting module or plugin.
 * @param message Non-null null-terminated string containing the log message.
 *
 * @note Thread Safety: Safe to call concurrently from any thread without locking.
 */
void TE_LogWrite(TE_LogLevel level, const char* module, const char* message);

/**
 * Register the global log callback handler used by TE_LogWrite.
 * Pass NULL to unregister the callback and disable log forwarding.
 *
 * @param callback Function pointer to receive log events, or NULL.
 *
 * @note Thread Safety: Should be configured during engine startup before multiple threads run.
 */
void TE_LogSetCallback(TE_LogFunc callback);

#ifdef __cplusplus
}
#endif
