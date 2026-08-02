#pragma once

#include "te_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TE_IPC_MAGIC 0x54454950u /* 'TEIP' */
#define TE_IPC_VERSION 1u
#define TE_IPC_MAX_PAYLOAD (64u * 1024u)

typedef enum TE_IpcMsgType {
    TE_IPC_MSG_SHUTDOWN = 1,
    TE_IPC_MSG_SHUTDOWN_COMPLETE = 2,
    TE_IPC_MSG_GET_PLUGIN_LIST = 3,
    TE_IPC_MSG_PLUGIN_LIST = 4,
    TE_IPC_MSG_ENABLE_PLUGIN = 5,
    TE_IPC_MSG_DISABLE_PLUGIN = 6,
    TE_IPC_MSG_RELOAD_CONFIG = 7,
    TE_IPC_MSG_STATUS = 8,
    TE_IPC_MSG_GET_SETTINGS = 9,
    TE_IPC_MSG_SETTINGS_RESPONSE = 10,
    TE_IPC_MSG_GET_PERF_STATS = 11,
    TE_IPC_MSG_PERF_STATS_RESPONSE = 12
} TE_IpcMsgType;

typedef struct TE_IpcHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t payload_length;
} TE_IpcHeader;

#ifdef __cplusplus
}
#endif
