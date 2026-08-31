#include "core/ipc_server.h"
#include <sdk/te_ipc.h>
#include <sdk/te_log.h>
#include "core/core_manager.h"
#include <sddl.h>
#include <stdio.h>

static struct {
    HANDLE hPipe;
    HANDLE hThread;
    HANDLE hStopEvent;
    HWND taskbar_hwnd;
    BOOL is_running;
} g_ipc;

#include <cJSON.h>
#include "core/plugin_loader.h"

static char* GenerateSettingsJson(void) {
    cJSON* root = cJSON_CreateObject();
    cJSON* plugins_arr = cJSON_AddArrayToObject(root, "plugins");
    
    int count = TE_PluginLoaderGetCount();
    for (int i = 0; i < count; i++) {
        TE_PluginEntry* entry = TE_PluginLoaderGetEntry(i);
        if (!entry || !entry->interface_ptr) continue;
        
        const PluginMetadata* meta = entry->interface_ptr->GetMetadata();
        const PluginSettings* settings = entry->interface_ptr->GetSettings();
        if (!meta || !settings || settings->count == 0) continue;
        
        cJSON* plugin_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(plugin_obj, "name", meta->name);
        
        cJSON* settings_arr = cJSON_AddArrayToObject(plugin_obj, "settings");
        for (uint32_t j = 0; j < settings->count; j++) {
            const SettingDescriptor* desc = &settings->descriptors[j];
            cJSON* setting_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(setting_obj, "key", desc->key);
            cJSON_AddStringToObject(setting_obj, "label", desc->label);
            if (desc->tooltip) cJSON_AddStringToObject(setting_obj, "tooltip", desc->tooltip);
            
            switch (desc->type) {
                case TE_SETTING_BOOL:
                    cJSON_AddStringToObject(setting_obj, "type", "bool");
                    cJSON_AddBoolToObject(setting_obj, "default", desc->value.bool_val.default_val);
                    break;
                case TE_SETTING_INT:
                    cJSON_AddStringToObject(setting_obj, "type", "int");
                    cJSON_AddNumberToObject(setting_obj, "default", desc->value.int_val.default_val);
                    cJSON_AddNumberToObject(setting_obj, "min", desc->value.int_val.min_val);
                    cJSON_AddNumberToObject(setting_obj, "max", desc->value.int_val.max_val);
                    cJSON_AddNumberToObject(setting_obj, "step", desc->value.int_val.step);
                    break;
                case TE_SETTING_FLOAT:
                    cJSON_AddStringToObject(setting_obj, "type", "float");
                    cJSON_AddNumberToObject(setting_obj, "default", desc->value.float_val.default_val);
                    cJSON_AddNumberToObject(setting_obj, "min", desc->value.float_val.min_val);
                    cJSON_AddNumberToObject(setting_obj, "max", desc->value.float_val.max_val);
                    cJSON_AddNumberToObject(setting_obj, "step", desc->value.float_val.step);
                    break;
                case TE_SETTING_STRING:
                    cJSON_AddStringToObject(setting_obj, "type", "string");
                    cJSON_AddStringToObject(setting_obj, "default", desc->value.string_val.default_val);
                    break;
                case TE_SETTING_ENUM: {
                    cJSON_AddStringToObject(setting_obj, "type", "enum");
                    if (desc->value.enum_val.default_val >= 0 && desc->value.enum_val.default_val < desc->value.enum_val.option_count) {
                        cJSON_AddStringToObject(setting_obj, "default", desc->value.enum_val.options[desc->value.enum_val.default_val]);
                    }
                    cJSON* opt_arr = cJSON_AddArrayToObject(setting_obj, "options");
                    for (int k = 0; k < desc->value.enum_val.option_count; k++) {
                        cJSON_AddItemToArray(opt_arr, cJSON_CreateString(desc->value.enum_val.options[k]));
                    }
                    break;
                }
                case TE_SETTING_COLOR:
                    cJSON_AddStringToObject(setting_obj, "type", "color");
                    cJSON_AddNumberToObject(setting_obj, "default", desc->value.color_val.default_val);
                    break;
            }
            cJSON_AddItemToArray(settings_arr, setting_obj);
        }
        cJSON_AddItemToArray(plugins_arr, plugin_obj);
    }
    
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

static HRESULT TE_IpcServerReadExact(HANDLE pipe, void* buffer, DWORD bytes, LPOVERLAPPED ol) {
    DWORD total = 0;
    HANDLE wait_handles[2] = { ol->hEvent, g_ipc.hStopEvent };
    while (total < bytes) {
        DWORD read_now = 0;
        ResetEvent(ol->hEvent);
        ol->Offset = 0; ol->OffsetHigh = 0;
        if (!ReadFile(pipe, (uint8_t*)buffer + total, bytes - total, &read_now, ol)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD wait_res = WaitForMultipleObjects(2, wait_handles, FALSE, 3000);
                if (wait_res == WAIT_OBJECT_0) {
                    if (!GetOverlappedResult(pipe, ol, &read_now, FALSE)) {
                        return HRESULT_FROM_WIN32(GetLastError());
                    }
                } else {
                    CancelIo(pipe);
                    return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
                }
            } else {
                return HRESULT_FROM_WIN32(err);
            }
        }
        if (read_now == 0) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        total += read_now;
    }
    return S_OK;
}

static HRESULT TE_IpcServerWriteExact(HANDLE pipe, const void* buffer, DWORD bytes, LPOVERLAPPED ol) {
    DWORD total = 0;
    HANDLE wait_handles[2] = { ol->hEvent, g_ipc.hStopEvent };
    while (total < bytes) {
        DWORD written = 0;
        ResetEvent(ol->hEvent);
        ol->Offset = 0; ol->OffsetHigh = 0;
        if (!WriteFile(pipe, (const uint8_t*)buffer + total, bytes - total, &written, ol)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD wait_res = WaitForMultipleObjects(2, wait_handles, FALSE, 3000);
                if (wait_res == WAIT_OBJECT_0) {
                    if (!GetOverlappedResult(pipe, ol, &written, FALSE)) {
                        return HRESULT_FROM_WIN32(GetLastError());
                    }
                } else {
                    CancelIo(pipe);
                    return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
                }
            } else {
                return HRESULT_FROM_WIN32(err);
            }
        }
        if (written == 0) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        total += written;
    }
    return S_OK;
}

static DWORD WINAPI TE_IpcServerThread(LPVOID lpParam) {
    (void)lpParam;
    OVERLAPPED ol = {0};
    ol.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ol.hEvent) return 1;

    HANDLE handles[2] = { ol.hEvent, g_ipc.hStopEvent };

    while (g_ipc.is_running) {
        if (!ConnectNamedPipe(g_ipc.hPipe, &ol)) {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                SetEvent(ol.hEvent);
            } else if (err != ERROR_IO_PENDING) {
                char msg[128];
                snprintf(msg, sizeof(msg), "ConnectNamedPipe failed: %u", err);
                TE_LogWrite(TE_LOG_ERROR, "IpcServer", msg);
                Sleep(100);
                continue;
            }
        }

        DWORD wait_res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait_res == WAIT_OBJECT_0 + 1) { /* Stop event */
            CancelIo(g_ipc.hPipe);
            break;
        }

        DWORD bytes_transferred = 0;
        if (GetOverlappedResult(g_ipc.hPipe, &ol, &bytes_transferred, FALSE)) {
            TE_IpcHeader header;
            if (SUCCEEDED(TE_IpcServerReadExact(g_ipc.hPipe, &header, sizeof(header), &ol))) {
                if (SUCCEEDED(TE_IpcValidateHeader(&header))) {
                    uint8_t payload[64 * 1024 + 1]; /* MAX_PAYLOAD + 1 for null terminator */
                    if (header.payload_length == 0 || SUCCEEDED(TE_IpcServerReadExact(g_ipc.hPipe, payload, header.payload_length, &ol))) {
                        payload[header.payload_length] = '\0';
                        if (header.type == TE_IPC_MSG_RELOAD_CONFIG) {
                            PostMessage(g_ipc.taskbar_hwnd, WM_TE_IPC_COMMAND, TE_CMD_RELOAD_CONFIG, 0);
                            TE_IpcHeader resp_hdr;
                            TE_IpcBuildHeader(&resp_hdr, TE_IPC_MSG_STATUS, 0);
                            TE_IpcServerWriteExact(g_ipc.hPipe, &resp_hdr, sizeof(resp_hdr), &ol);
                        } else if (header.type == TE_IPC_MSG_SHUTDOWN) {
                            PostMessage(g_ipc.taskbar_hwnd, WM_TE_IPC_COMMAND, TE_CMD_SHUTDOWN, 0);
                            TE_IpcHeader resp_hdr;
                            TE_IpcBuildHeader(&resp_hdr, TE_IPC_MSG_STATUS, 0);
                            TE_IpcServerWriteExact(g_ipc.hPipe, &resp_hdr, sizeof(resp_hdr), &ol);
                        } else if (header.type == TE_IPC_MSG_ENABLE_PLUGIN || header.type == TE_IPC_MSG_DISABLE_PLUGIN) {
                            char* name_copy = _strdup((const char*)payload);
                            if (name_copy) {
                                int cmd = (header.type == TE_IPC_MSG_ENABLE_PLUGIN) ? TE_CMD_ENABLE_PLUGIN : TE_CMD_DISABLE_PLUGIN;
                                if (!PostMessage(g_ipc.taskbar_hwnd, WM_TE_IPC_COMMAND, cmd, (LPARAM)name_copy)) {
                                    free(name_copy);
                                }
                                TE_IpcHeader resp_hdr;
                                TE_IpcBuildHeader(&resp_hdr, TE_IPC_MSG_STATUS, 0);
                                TE_IpcServerWriteExact(g_ipc.hPipe, &resp_hdr, sizeof(resp_hdr), &ol);
                            }
                        } else if (header.type == TE_IPC_MSG_GET_SETTINGS) {
                            char* json_str = GenerateSettingsJson();
                            if (json_str) {
                                uint32_t len = (uint32_t)strlen(json_str);
                                TE_IpcHeader resp_hdr;
                                TE_IpcBuildHeader(&resp_hdr, TE_IPC_MSG_SETTINGS_RESPONSE, len);
                                TE_IpcServerWriteExact(g_ipc.hPipe, &resp_hdr, sizeof(resp_hdr), &ol);
                                TE_IpcServerWriteExact(g_ipc.hPipe, json_str, len, &ol);
                                cJSON_free(json_str);
                            }
                        } else if (header.type == TE_IPC_MSG_GET_PERF_STATS) {
                            const char* stats_json = "{\"fps\":60.0,\"avg_ms\":0.5,\"min_ms\":0.1,\"max_ms\":1.2}";
                            uint32_t len = (uint32_t)strlen(stats_json);
                            TE_IpcHeader resp_hdr;
                            TE_IpcBuildHeader(&resp_hdr, TE_IPC_MSG_PERF_STATS_RESPONSE, len);
                            TE_IpcServerWriteExact(g_ipc.hPipe, &resp_hdr, sizeof(resp_hdr), &ol);
                            TE_IpcServerWriteExact(g_ipc.hPipe, stats_json, len, &ol);
                        } else if (header.type == TE_IPC_MSG_GET_PLUGIN_LIST) {
                            char* json_str = GenerateSettingsJson();
                            if (json_str) {
                                uint32_t len = (uint32_t)strlen(json_str);
                                TE_IpcHeader resp_hdr;
                                TE_IpcBuildHeader(&resp_hdr, TE_IPC_MSG_PLUGIN_LIST, len);
                                TE_IpcServerWriteExact(g_ipc.hPipe, &resp_hdr, sizeof(resp_hdr), &ol);
                                TE_IpcServerWriteExact(g_ipc.hPipe, json_str, len, &ol);
                                cJSON_free(json_str);
                            }
                        }
                    }
                }
            }
            DisconnectNamedPipe(g_ipc.hPipe);
        }
        ResetEvent(ol.hEvent);
    }
    
    CloseHandle(ol.hEvent);
    return 0;
}

HRESULT TE_IpcServerStart(HWND taskbar_hwnd) {
    if (g_ipc.is_running) return TE_S_OK;
    
    g_ipc.taskbar_hwnd = taskbar_hwnd;
    g_ipc.hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_ipc.hStopEvent) return TE_E_FAIL;
    
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    /* D:(A;;GA;;;OW) - Allow Generic All to Owner */
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;OW)", SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL);
    
    g_ipc.hPipe = CreateNamedPipeW(
        TE_PIPE_NAME,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, /* Max instances */
        TE_IPC_MAX_PAYLOAD,
        TE_IPC_MAX_PAYLOAD,
        0,
        sa.lpSecurityDescriptor ? &sa : NULL
    );
    
    if (sa.lpSecurityDescriptor) {
        LocalFree(sa.lpSecurityDescriptor);
    }
    
    if (g_ipc.hPipe == INVALID_HANDLE_VALUE) {
        CloseHandle(g_ipc.hStopEvent);
        g_ipc.hStopEvent = NULL;
        return TE_E_FAIL;
    }
    
    g_ipc.is_running = TRUE;
    g_ipc.hThread = CreateThread(NULL, 0, TE_IpcServerThread, NULL, 0, NULL);
    if (!g_ipc.hThread) {
        g_ipc.is_running = FALSE;
        CloseHandle(g_ipc.hPipe);
        CloseHandle(g_ipc.hStopEvent);
        g_ipc.hPipe = NULL;
        g_ipc.hStopEvent = NULL;
        return TE_E_FAIL;
    }
    
    TE_LogWrite(TE_LOG_INFO, "IpcServer", "Named pipe server started");
    return TE_S_OK;
}

void TE_IpcServerStop(void) {
    if (!g_ipc.is_running) return;
    
    g_ipc.is_running = FALSE;
    SetEvent(g_ipc.hStopEvent);
    
    if (g_ipc.hThread) {
        WaitForSingleObject(g_ipc.hThread, 5000);
        CloseHandle(g_ipc.hThread);
        g_ipc.hThread = NULL;
    }
    
    if (g_ipc.hPipe) {
        CloseHandle(g_ipc.hPipe);
        g_ipc.hPipe = NULL;
    }
    
    if (g_ipc.hStopEvent) {
        CloseHandle(g_ipc.hStopEvent);
        g_ipc.hStopEvent = NULL;
    }
    
    TE_LogWrite(TE_LOG_INFO, "IpcServer", "Named pipe server stopped");
}
