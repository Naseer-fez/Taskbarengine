#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TE_IconEntry {
    wchar_t app_id[256];
    HBITMAP bitmap;
    int width;
    int height;
} TE_IconEntry;

/**
 * @brief Initialize icon capture cache subsystem.
 */
HRESULT TE_IconCaptureInit(void);

/**
 * @brief Retrieve cached bitmap or extract new icon bitmap for given app_id.
 */
HRESULT TE_IconCaptureGet(const wchar_t* app_id, TE_IconEntry* out_entry);

/**
 * @brief Invalidate cached icon entry by app_id.
 */
void TE_IconCaptureInvalidate(const wchar_t* app_id);

/**
 * @brief Shutdown icon capture subsystem and free all cached HBITMAP handles.
 */
void TE_IconCaptureShutdown(void);

#ifdef __cplusplus
}
#endif
