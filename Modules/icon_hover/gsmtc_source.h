#pragma once

#include <memory>
#include "dynamic_island_media.h"

// Concrete implementation of IMediaSource backed by Windows C++/WinRT
// GlobalSystemMediaTransportControlsSessionManager (GSMTC).
// WinRT headers are strictly isolated to gsmtc_source.cpp via the PIMPL pattern.
class GsmtcMediaSource final : public IMediaSource {
public:
    GsmtcMediaSource();
    ~GsmtcMediaSource() override;

    // IMediaSource interface implementation
    bool Initialize() override;
    void Shutdown() override;

    bool HasNewState() const override;
    bool GetCurrentSnapshot(TEMediaStateSnapshot* outSnapshot) override;

    void TogglePlayPauseAsync() override;
    void PlayAsync() override;
    void PauseAsync() override;
    void NextAsync() override;
    void PreviousAsync() override;

    void SetWakeCallback(void (*callback)(void)) override;

    // Query whether background worker is running
    bool IsRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Factory function to instantiate GsmtcMediaSource as an IMediaSource
std::unique_ptr<IMediaSource> CreateGsmtcMediaSource();
