#pragma once

#include "dynamic_island_media_types.h"

// Pure C++ abstract interface for media transport sources (GSMTC, mocks, etc.)
// No WinRT or heavy Windows headers are included here.
class IMediaSource {
public:
    virtual ~IMediaSource() = default;

    // Subsystem lifecycle
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    // Cross-thread state queries (called by UI / frame loop thread)
    virtual bool HasNewState() const = 0;
    virtual bool GetCurrentSnapshot(TEMediaStateSnapshot* outSnapshot) = 0;

    // Asynchronous playback control commands (called on UI thread without blocking)
    virtual void TogglePlayPauseAsync() = 0;
    virtual void PlayAsync() = 0;
    virtual void PauseAsync() = 0;
    virtual void NextAsync() = 0;
    virtual void PreviousAsync() = 0;

    // Wake callback integration for background updates
    virtual void SetWakeCallback(void (*callback)(void)) { (void)callback; }
};
