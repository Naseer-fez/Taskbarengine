#include "sdk/te_version.h"
#include <windows.h>

/**
 * RtlGetVersion is an ntdll function that returns the true OS version
 * without being affected by application compatibility manifests.
 * We load it dynamically to avoid a hard link dependency on ntdll.lib.
 */
typedef LONG(WINAPI* RtlGetVersionFunc)(PRTL_OSVERSIONINFOW);

static TE_WindowsVersion g_cached_version = { 0 };
static volatile LONG g_version_initialized = 0;

/**
 * @brief Read the Update Build Revision (UBR) from the registry.
 *
 * The UBR is the 4th component of the full Windows version
 * (e.g., 10.0.22621.xxxx) and is not available via RtlGetVersion.
 *
 * @return UBR value, or 0 if the registry key cannot be read.
 */
static uint32_t ReadUBR(void)
{
    HKEY hkey = NULL;
    LONG status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_READ, &hkey);
    if (status != ERROR_SUCCESS) {
        return 0;
    }

    DWORD ubr = 0;
    DWORD size = sizeof(ubr);
    DWORD type = 0;
    status = RegQueryValueExW(hkey, L"UBR", NULL, &type, (LPBYTE)&ubr, &size);
    RegCloseKey(hkey);

    if (status != ERROR_SUCCESS || type != REG_DWORD) {
        return 0;
    }
    return (uint32_t)ubr;
}

HRESULT TE_GetWindowsVersion(TE_WindowsVersion* version)
{
    if (!version) {
        return E_POINTER;
    }

    /* One-shot initialization: first thread to swap 0→1 performs the query */
    if (InterlockedCompareExchange(&g_version_initialized, 1, 0) == 0) {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) {
            InterlockedExchange(&g_version_initialized, -1);
            return E_FAIL;
        }

        RtlGetVersionFunc rtl_get_version =
            (RtlGetVersionFunc)(void*)GetProcAddress(ntdll, "RtlGetVersion");
        if (!rtl_get_version) {
            InterlockedExchange(&g_version_initialized, -1);
            return E_FAIL;
        }

        RTL_OSVERSIONINFOW vi;
        ZeroMemory(&vi, sizeof(vi));
        vi.dwOSVersionInfoSize = sizeof(vi);

        LONG ntstatus = rtl_get_version(&vi);
        if (ntstatus != 0 /* STATUS_SUCCESS */) {
            InterlockedExchange(&g_version_initialized, -1);
            return E_FAIL;
        }

        g_cached_version.major    = (uint32_t)vi.dwMajorVersion;
        g_cached_version.minor    = (uint32_t)vi.dwMinorVersion;
        g_cached_version.build    = (uint32_t)vi.dwBuildNumber;
        g_cached_version.revision = ReadUBR();
        g_cached_version.is_server =
            (vi.dwPlatformId == VER_PLATFORM_WIN32_NT &&
             vi.dwMajorVersion >= 10 &&
             vi.dwBuildNumber >= 20000)
            ? FALSE /* Cannot reliably detect server from RtlGetVersion alone;
                       leave as FALSE for desktop-only taskbar tool */
            : FALSE;

        /* Ensure all fields are visible before marking as initialized.
         * InterlockedExchange provides a full memory barrier. */
        InterlockedExchange(&g_version_initialized, 2);
    }

    /* Wait for the initializing thread to finish (extremely brief) */
    while (InterlockedCompareExchange(&g_version_initialized, 0, 0) == 1) {
        YieldProcessor();
    }

    if (g_version_initialized != 2) {
        return E_FAIL;
    }

    *version = g_cached_version;
    return S_OK;
}

BOOL TE_IsBuildSupported(uint32_t build)
{
    return build >= 22621;
}

BOOL TE_IsBuildInRange(uint32_t build, uint32_t min_build, uint32_t max_build)
{
    if (min_build > 0 && build < min_build) {
        return FALSE;
    }
    if (max_build > 0 && build > max_build) {
        return FALSE;
    }
    return TRUE;
}
