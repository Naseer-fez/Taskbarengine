#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sets the DLL HINSTANCE.
 */
void TE_SetEngineInstance(HINSTANCE hinstance);

/**
 * @brief Initializes the Core Engine Phase A (lightweight, runs in hook).
 * @return S_OK on success.
 * @note Thread safety: Thread-safe, idempotent initialization.
 */
HRESULT TE_EngineInitialize(void);

/**
 * @brief Initializes the Core Engine Phase B (heavy, runs deferred).
 * @return S_OK on success.
 */
HRESULT TE_EngineInitializeDeferred(void);

/**
 * @brief CBT Hook Procedure exported for SetWindowsHookEx.
 * @param nCode Hook code specifying action.
 * @param wParam Hook-specific parameter.
 * @param lParam Hook-specific parameter.
 * @return CallNextHookEx result.
 * @note Thread safety: Called on Explorer threads processing Windows hooks.
 */
TE_API LRESULT CALLBACK TE_CbtHookProc(int nCode, WPARAM wParam, LPARAM lParam);

#ifdef __cplusplus
}
#endif
