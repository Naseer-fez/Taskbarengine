#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Current TaskbarEngine SDK API version. */
#define TE_API_VERSION 1

/** DLL export specifier for TaskbarEngine plugins and core libraries. */
#define TE_EXPORT __declspec(dllexport)

/** DLL import specifier for TaskbarEngine consumer modules. */
#define TE_IMPORT __declspec(dllimport)

/** Evaluate whether an HRESULT indicates success. */
#define TE_SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)

/** Evaluate whether an HRESULT indicates failure. */
#define TE_FAILED(hr) (((HRESULT)(hr)) < 0)

/** HRESULT code indicating successful operation. */
#define TE_S_OK ((HRESULT)0L)

/** HRESULT code indicating unspecified failure. */
#define TE_E_FAIL ((HRESULT)0x80004005L)

/** HRESULT code indicating one or more invalid arguments. */
#define TE_E_INVALIDARG ((HRESULT)0x80070057L)

/** HRESULT code indicating memory allocation failure. */
#define TE_E_OUTOFMEMORY ((HRESULT)0x8007000EL)

/** HRESULT code indicating an unimplemented function or interface. */
#define TE_E_NOTIMPL ((HRESULT)0x80004001L)

/**
 * Custom window message for Phase B engine initialization.
 * Posted to the taskbar window after CBT hook injection to perform initialization
 * on the UI thread outside loader lock (where COM, LoadLibrary, etc. are safe).
 */
#define WM_TE_INIT (WM_APP + 100)

/**
 * Custom window message to marshal IPC commands to the UI thread.
 * State-mutating operations received on background IPC threads are marshaled
 * to the Explorer UI thread via this message to maintain single-threaded safety.
 */
#define WM_TE_IPC_COMMAND (WM_APP + 101)

/**
 * Custom window message for UI thread timer ticks.
 */
#define WM_TE_TIMER_TICK (WM_APP + 102)

/**
 * Subclass identifier used when subclassing Shell_TrayWnd via SetWindowSubclass.
 * Represents ASCII 'TBEN' (0x5442454E).
 */
#define TE_SUBCLASS_ID 0x5442454E  /* 'TBEN' */

/**
 * Forward declaration of PluginContext for struct size compatibility checks.
 */
typedef struct PluginContext PluginContext;

/**
 * Macro to check whether a PluginContext instance contains a specific field,
 * based on the struct_size field in the ABI envelope.
 * Used to ensure backwards/forwards binary compatibility across SDK revisions.
 */
#define TE_CTX_HAS_FIELD(ctx, field) \
    ((ctx)->struct_size >= (uint32_t)(offsetof(PluginContext, field) + sizeof((ctx)->field)))

#ifdef __cplusplus
}
#endif
