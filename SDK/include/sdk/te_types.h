#pragma once

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Export/Import macros for DLL boundaries.
 */
#define TE_EXPORT __declspec(dllexport)
#define TE_IMPORT __declspec(dllimport)

#ifdef TE_EXPORTS
#define TE_API TE_EXPORT
#else
#define TE_API TE_IMPORT
#endif

/**
 * @brief API version macro for ABI backwards compatibility checks.
 */
#define TE_API_VERSION 1

/**
 * @brief HRESULT validation macros.
 */
#define TE_SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define TE_FAILED(hr)    (((HRESULT)(hr)) < 0)

#ifdef __cplusplus
}
#endif
