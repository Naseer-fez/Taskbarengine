#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write a debug trace message to a fixed log file AND OutputDebugStringA.
 * 
 * Safe to call from any context EXCEPT DllMain (loader lock).
 * Thread-safe via mutex.
 */
void TE_DebugTrace(const char* msg);

/**
 * @brief Formatted version of TE_DebugTrace.
 */
void TE_DebugTraceFmt(const char* fmt, ...);

#ifdef __cplusplus
}
#endif
