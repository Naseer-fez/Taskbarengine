#pragma once

#include <cstring>
#include <cwchar>
#include <cstdint>
#include "../Modules/icon_hover/dynamic_island_media.h"
#include "../Modules/icon_hover/media_state_bridge.h"

// Deterministic mock media source implementing IMediaSource backed by MediaStateBridge.
// Used for Catch2 unit testing without requiring active Windows media players or audio hardware.
class MockMediaSource final : public IMediaSource {
public:
    MockMediaSource() {
        std::memset(&m_snapshot, 0, sizeof(m_snapshot));
    }

    ~MockMediaSource() override = default;

    bool Initialize() override {
        m_initialized = true;
        return true;
    }

    void Shutdown() override {
        m_initialized = false;
        m_bridge.Reset();
    }

    bool HasNewState() const override {
        return m_bridge.HasPendingChange();
    }

    bool GetCurrentSnapshot(TEMediaStateSnapshot* outSnapshot) override {
        return m_bridge.ConsumeSnapshot(outSnapshot);
    }

    bool GetLatestSnapshot(TEMediaStateSnapshot* outSnapshot) const {
        return m_bridge.GetLatestSnapshot(outSnapshot);
    }

    void SetMockTrack(const wchar_t* title, const wchar_t* artist, TEMediaPlaybackStatus status,
                      const wchar_t* album = L"", uint64_t duration_ms = 180000, uint64_t position_ms = 0) {
        if (title != nullptr) {
            wcsncpy_s(m_snapshot.title, title, _TRUNCATE);
        } else {
            m_snapshot.title[0] = L'\0';
        }

        if (artist != nullptr) {
            wcsncpy_s(m_snapshot.artist, artist, _TRUNCATE);
        } else {
            m_snapshot.artist[0] = L'\0';
        }

        if (album != nullptr) {
            wcsncpy_s(m_snapshot.album, album, _TRUNCATE);
        } else {
            m_snapshot.album[0] = L'\0';
        }

        m_snapshot.status = status;
        m_snapshot.has_media = (status != TEMediaPlaybackStatus::Inactive);
        m_snapshot.position_ms = position_ms;
        m_snapshot.duration_ms = duration_ms;
        m_snapshot.sequence_number = ++m_seq;
        m_snapshot.track_change_id = ++m_trackId;
        m_bridge.PublishSnapshot(m_snapshot);
    }

    void SimulateStatusChange(TEMediaPlaybackStatus status) {
        m_snapshot.status = status;
        m_snapshot.has_media = (status != TEMediaPlaybackStatus::Inactive);
        m_snapshot.sequence_number = ++m_seq;
        m_bridge.PublishSnapshot(m_snapshot);
    }

    void SimulatePositionChange(uint64_t position_ms, bool wakeAnimation = false) {
        m_snapshot.position_ms = position_ms;
        m_snapshot.sequence_number = ++m_seq;
        m_bridge.PublishSnapshot(m_snapshot, wakeAnimation);
    }

    void TogglePlayPauseAsync() override {
        if (m_snapshot.status == TEMediaPlaybackStatus::Playing) {
            SimulateStatusChange(TEMediaPlaybackStatus::Paused);
        } else {
            SimulateStatusChange(TEMediaPlaybackStatus::Playing);
        }
    }

    void PlayAsync() override {
        SimulateStatusChange(TEMediaPlaybackStatus::Playing);
    }

    void PauseAsync() override {
        SimulateStatusChange(TEMediaPlaybackStatus::Paused);
    }

    void NextAsync() override {}
    void PreviousAsync() override {}

    void SetWakeCallback(void (*callback)(void)) override {
        m_bridge.SetWakeCallback(callback);
    }

    bool IsInitialized() const {
        return m_initialized;
    }

private:
    MediaStateBridge m_bridge;
    TEMediaStateSnapshot m_snapshot{};
    uint64_t m_seq{0};
    uint64_t m_trackId{0};
    bool m_initialized{false};
};
