#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TE_IconEntry {
    wchar_t app_id[256];
    HBITMAP bitmap;
    int width;
    int height;
    bool valid;
} TE_IconEntry;

HRESULT TE_IconCaptureInit(void);
HRESULT TE_IconCaptureGet(const wchar_t* app_id, TE_IconEntry* out_entry);
void TE_IconCaptureInvalidate(const wchar_t* app_id);
void TE_IconCaptureShutdown(void);

#ifdef __cplusplus
}
#endif
