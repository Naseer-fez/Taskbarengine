#pragma once

#include <cstdint>
#include <cwchar>

#define TE_ISLAND_STRING_MAX 128

enum class TEMediaPlaybackStatus : uint32_t {
    Inactive = 0,
    Stopped  = 1,
    Playing  = 2,
    Paused   = 3
};

struct TEMediaStateSnapshot {
    wchar_t title[TE_ISLAND_STRING_MAX];
    wchar_t artist[TE_ISLAND_STRING_MAX];
    wchar_t album[TE_ISLAND_STRING_MAX];
    TEMediaPlaybackStatus status;
    bool has_media;
    uint64_t position_ms;
    uint64_t duration_ms;
    uint64_t sequence_number;
    uint64_t track_change_id;
};
