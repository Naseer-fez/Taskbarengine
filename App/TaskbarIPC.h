#pragma once

#include <windows.h>

#define TE_IPC_PIPE_NAME L"\\\\.\\pipe\\TaskbarEngineIPC"

enum TE_IPC_MessageType {
    TE_IPC_MSG_UPDATE_CONFIG = 1,
    TE_IPC_MSG_UNINJECT = 2,
    TE_IPC_MSG_UNINJECT_ACK = 3
};

struct TE_IPC_Config {
    float max_scale;
    float spread_width;
    float spring_stiffness;
    float spring_damping;
    bool enable_secondary_monitors;
};

struct TE_IPC_Message {
    TE_IPC_MessageType type;
    union {
        TE_IPC_Config config;
    } data;
};
