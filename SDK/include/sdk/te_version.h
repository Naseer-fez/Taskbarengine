#pragma once

#include "te_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Detected Windows version information.
 *
 * Populated once via RtlGetVersion (ntdll.dll) and cached for the
 * lifetime of the process. Not subject to application compatibility shims.
 */
typedef struct TE_WindowsVersion {
    uint32_t major;       /**< e.g. 10 */
    uint32_t minor;       /**< e.g. 0 */
    uint32_t build;       /**< e.g. 22621 (Win11 22H2) */
    uint32_t revision;    /**< UBR from registry, 0 if unavailable */
    BOOL     is_server;   /**< TRUE for Windows Server editions */
} TE_WindowsVersion;

/**
 * @brief Plugin build compatibility declaration.
 *
 * Plugins declare the Windows build range they support.
 * Zero values mean "no restriction declared."
 */
typedef struct TE_PluginCompatibility {
    uint32_t min_build;         /**< Minimum Windows build required (0 = any) */
    uint32_t max_tested_build;  /**< Highest build tested against (0 = any) */
} TE_PluginCompatibility;

/**
 * @brief Retrieve the cached Windows version information.
 *
 * On first call, queries RtlGetVersion from ntdll.dll and caches the
 * result. Subsequent calls return the cached value with zero overhead.
 *
 * @param[out] version Pointer to receive version information.
 * @return S_OK on success, E_POINTER if version is NULL, E_FAIL if
 *         RtlGetVersion could not be resolved.
 * @note Thread safety: Thread-safe (interlocked one-shot initialization).
 */
HRESULT TE_GetWindowsVersion(TE_WindowsVersion* version);

/**
 * @brief Check whether a Windows build is officially supported by TaskbarEngine.
 *
 * Supported baseline is Windows 11 22H2 (build 22621) and above.
 *
 * @param build The Windows build number to test.
 * @return TRUE if build >= 22621, FALSE otherwise.
 */
BOOL TE_IsBuildSupported(uint32_t build);

/**
 * @brief Check whether a Windows build number is in the supported range.
 *
 * @param build     The Windows build number to test.
 * @param min_build Minimum required build (0 = no minimum).
 * @param max_build Maximum tested build (0 = no maximum).
 * @return TRUE if the build is within [min_build, max_build], accounting
 *         for zero-means-unbounded semantics.
 */
BOOL TE_IsBuildInRange(uint32_t build, uint32_t min_build, uint32_t max_build);

#ifdef __cplusplus
}
#endif
