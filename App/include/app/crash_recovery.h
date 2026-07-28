#pragma once

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TE_CrashRecoveryState {
    TE_CRASH_RECOVERY_RUNNING = 0,
    TE_CRASH_RECOVERY_EXPLORER_DEAD,
    TE_CRASH_RECOVERY_WAITING_TASKBAR_CREATED,
    TE_CRASH_RECOVERY_REHOOKING
} TE_CrashRecoveryState;

typedef HRESULT (*TE_ReinstallHookFunc)(void* context);

HRESULT TE_CrashRecoveryStart(HWND hwnd, DWORD explorer_pid, TE_ReinstallHookFunc reinstall_hook, void* context);
void TE_CrashRecoveryStop(void);
TE_CrashRecoveryState TE_CrashRecoveryGetState(void);
TE_CrashRecoveryState TE_CrashRecoveryAdvance(TE_CrashRecoveryState state, UINT msg, bool explorer_dead);

#ifdef __cplusplus
}
#endif
