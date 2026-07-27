#pragma once

#include <sdk/te_types.h>
#include <cJSON.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

HRESULT TE_ConfigResolvePath(wchar_t* buf, size_t buf_len);
HRESULT TE_ConfigLoad(const wchar_t* path, cJSON** out_root);
const cJSON* TE_ConfigGetPluginSection(const cJSON* root, const char* plugin_name);
const cJSON* TE_ConfigGetCoreSection(const cJSON* root);

#ifdef __cplusplus
}
#endif
