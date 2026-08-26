#pragma once

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register TaskbarEngine to run at logon using Task Scheduler.
 *        This bypasses UAC prompts if the user is administrator.
 * @param exe_path The absolute path to the TaskbarEngine.exe to run.
 * @return S_OK on success, or error HRESULT.
 */
HRESULT TE_SchedulerRegisterTask(const wchar_t* exe_path);

/**
 * @brief Remove the TaskbarEngine logon task.
 * @return S_OK on success, or error HRESULT.
 */
HRESULT TE_SchedulerRemoveTask(void);

#ifdef __cplusplus
}
#endif
