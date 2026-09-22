#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#undef GetCurrentTime

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <cstring>
#include <cwchar>

#include "gsmtc_source.h"
#include "media_state_bridge.h"

namespace {

enum class GsmtcCommand {
    None,
    RefreshSession,
    RefreshPlayback,
    RefreshProperties,
    RefreshTimeline,
    TogglePlayPause,
    Play,
    Pause,
    Next,
    Previous
};

} // anonymous namespace

struct GsmtcMediaSource::Impl {
    std::mutex m_lifecycleMutex;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_workerThread;

    std::mutex m_cmdMutex;
    std::condition_variable m_cv;
    std::queue<GsmtcCommand> m_cmdQueue;

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_sessionManager{nullptr};
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession m_currentSession{nullptr};

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::CurrentSessionChanged_revoker m_sessionChangedRevoker;
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession::PlaybackInfoChanged_revoker m_playbackChangedRevoker;
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession::MediaPropertiesChanged_revoker m_propChangedRevoker;
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession::TimelinePropertiesChanged_revoker m_timelineChangedRevoker;

    MediaStateBridge m_bridge;
    TEMediaStateSnapshot m_cachedSnapshot{};
    uint64_t m_seq{0};
    uint64_t m_trackId{0};

    Impl() {
        std::memset(&m_cachedSnapshot, 0, sizeof(m_cachedSnapshot));
    }

    ~Impl() {
        Shutdown();
    }

    bool Initialize() {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        if (m_running) {
            return true;
        }

        m_stopRequested = false;
        m_workerThread = std::thread(&Impl::WorkerThreadProc, this);
        m_running = true;
        return true;
    }

    void PostCommand(GsmtcCommand cmd) {
        if (!m_running.load(std::memory_order_relaxed)) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            m_cmdQueue.push(cmd);
        }
        m_cv.notify_one();
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        if (!m_running) {
            return;
        }

        m_stopRequested = true;
        m_cv.notify_all();

        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }

        {
            std::lock_guard<std::mutex> cmdLock(m_cmdMutex);
            std::queue<GsmtcCommand> empty;
            std::swap(m_cmdQueue, empty);
        }

        m_running = false;
        m_bridge.Reset();
    }

    void WorkerThreadProc() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            // Failed to initialize COM apartment on worker thread
            return;
        }

        try {
            auto asyncOp = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
            m_sessionManager = asyncOp.get();

            if (m_sessionManager) {
                m_sessionChangedRevoker = m_sessionManager.CurrentSessionChanged(
                    winrt::auto_revoke,
                    [this](auto const&, auto const&) {
                        PostCommand(GsmtcCommand::RefreshSession);
                    }
                );

                HandleRefreshSession();
            }
        } catch (...) {
            // GSMTC service unprovisioned or failed
            PublishInactive();
        }

        while (!m_stopRequested) {
            GsmtcCommand cmd = GsmtcCommand::None;
            {
                std::unique_lock<std::mutex> lock(m_cmdMutex);
                m_cv.wait_for(lock, std::chrono::milliseconds(2000), [this] {
                    return m_stopRequested.load() || !m_cmdQueue.empty();
                });

                if (m_stopRequested) {
                    break;
                }

                if (!m_cmdQueue.empty()) {
                    cmd = m_cmdQueue.front();
                    m_cmdQueue.pop();
                }
            }

            if (cmd != GsmtcCommand::None) {
                try {
                    ExecuteCommand(cmd);
                } catch (...) {
                    // Shield worker thread from unexpected exceptions
                }
            }
        }

        // Cleanup revokers and COM references before apartment uninitialization
        try {
            m_timelineChangedRevoker.revoke();
            m_propChangedRevoker.revoke();
            m_playbackChangedRevoker.revoke();
            m_sessionChangedRevoker.revoke();
            m_currentSession = nullptr;
            m_sessionManager = nullptr;
        } catch (...) {
        }

        try {
            winrt::uninit_apartment();
        } catch (...) {
        }
    }

    void HandleRefreshSession() {
        try {
            m_timelineChangedRevoker.revoke();
            m_propChangedRevoker.revoke();
            m_playbackChangedRevoker.revoke();

            if (!m_sessionManager) {
                PublishInactive();
                return;
            }

            m_currentSession = m_sessionManager.GetCurrentSession();
            if (m_currentSession) {
                m_playbackChangedRevoker = m_currentSession.PlaybackInfoChanged(
                    winrt::auto_revoke,
                    [this](auto const&, auto const&) {
                        PostCommand(GsmtcCommand::RefreshPlayback);
                    }
                );

                m_propChangedRevoker = m_currentSession.MediaPropertiesChanged(
                    winrt::auto_revoke,
                    [this](auto const&, auto const&) {
                        PostCommand(GsmtcCommand::RefreshProperties);
                    }
                );

                m_timelineChangedRevoker = m_currentSession.TimelinePropertiesChanged(
                    winrt::auto_revoke,
                    [this](auto const&, auto const&) {
                        PostCommand(GsmtcCommand::RefreshTimeline);
                    }
                );

                HandleRefreshPlayback();
                HandleRefreshProperties();
                HandleRefreshTimeline();
            } else {
                PublishInactive();
            }
        } catch (...) {
            PublishInactive();
        }
    }

    void HandleRefreshPlayback() {
        try {
            if (!m_currentSession) {
                PublishInactive();
                return;
            }

            auto info = m_currentSession.GetPlaybackInfo();
            auto status = info.PlaybackStatus();

            TEMediaPlaybackStatus teStatus = TEMediaPlaybackStatus::Inactive;
            switch (status) {
                case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
                    teStatus = TEMediaPlaybackStatus::Playing;
                    break;
                case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
                    teStatus = TEMediaPlaybackStatus::Paused;
                    break;
                case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
                    teStatus = TEMediaPlaybackStatus::Stopped;
                    break;
                case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed:
                case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Opened:
                case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing:
                default:
                    teStatus = TEMediaPlaybackStatus::Inactive;
                    break;
            }

            m_cachedSnapshot.status = teStatus;
            m_cachedSnapshot.has_media = (teStatus == TEMediaPlaybackStatus::Playing ||
                                          teStatus == TEMediaPlaybackStatus::Paused  ||
                                          teStatus == TEMediaPlaybackStatus::Stopped);
            m_cachedSnapshot.sequence_number = ++m_seq;
            m_bridge.PublishSnapshot(m_cachedSnapshot);
        } catch (...) {
        }
    }

    void HandleRefreshProperties() {
        try {
            if (!m_currentSession) {
                return;
            }

            /* PERF-505: Convert synchronous .get() to asynchronous continuation to avoid thread stalls */
            auto asyncMediaProps = m_currentSession.TryGetMediaPropertiesAsync();
            asyncMediaProps.Completed([this](auto&& asyncOp, winrt::Windows::Foundation::AsyncStatus status) {
                if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
                    return;
                }
                try {
                    auto mediaProps = asyncOp.GetResults();
                    if (!mediaProps) {
                        return;
                    }

                    winrt::hstring titleH = mediaProps.Title();
                    winrt::hstring artistH = mediaProps.Artist();
                    winrt::hstring albumH = mediaProps.AlbumTitle();

                    const wchar_t* titleStr = titleH.c_str();
                    const wchar_t* artistStr = artistH.c_str();
                    const wchar_t* albumStr = albumH.c_str();

                    bool trackChanged = (std::wcscmp(m_cachedSnapshot.title, titleStr) != 0) ||
                                        (std::wcscmp(m_cachedSnapshot.artist, artistStr) != 0);

                    wcsncpy_s(m_cachedSnapshot.title, titleStr, _TRUNCATE);
                    wcsncpy_s(m_cachedSnapshot.artist, artistStr, _TRUNCATE);
                    wcsncpy_s(m_cachedSnapshot.album, albumStr, _TRUNCATE);

                    m_cachedSnapshot.sequence_number = ++m_seq;
                    if (trackChanged) {
                        m_cachedSnapshot.track_change_id = ++m_trackId;
                    }

                    m_bridge.PublishSnapshot(m_cachedSnapshot);
                } catch (...) {
                }
            });
        } catch (...) {
        }
    }

    void HandleRefreshTimeline() {
        try {
            if (!m_currentSession) {
                return;
            }

            auto timeline = m_currentSession.GetTimelineProperties();
            auto pos = timeline.Position();
            auto end = timeline.EndTime();
            auto start = timeline.StartTime();
            auto dur = (end > start) ? (end - start) : winrt::Windows::Foundation::TimeSpan::zero();

            auto countMs = std::chrono::duration_cast<std::chrono::milliseconds>(pos).count();
            m_cachedSnapshot.position_ms = (countMs > 0) ? static_cast<uint64_t>(countMs) : 0;
            m_cachedSnapshot.duration_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());

            m_cachedSnapshot.sequence_number = ++m_seq;
            // PERF-501: Do not wake the animation frame loop on timeline position ticks
            m_bridge.PublishSnapshot(m_cachedSnapshot, false);
        } catch (...) {
        }
    }

    void PublishInactive() {
        std::memset(&m_cachedSnapshot, 0, sizeof(m_cachedSnapshot));
        m_cachedSnapshot.status = TEMediaPlaybackStatus::Inactive;
        m_cachedSnapshot.has_media = false;
        m_cachedSnapshot.sequence_number = ++m_seq;
        m_bridge.PublishSnapshot(m_cachedSnapshot);
    }

    void ExecuteCommand(GsmtcCommand cmd) {
        switch (cmd) {
            case GsmtcCommand::RefreshSession:
                HandleRefreshSession();
                break;
            case GsmtcCommand::RefreshPlayback:
                HandleRefreshPlayback();
                break;
            case GsmtcCommand::RefreshProperties:
                HandleRefreshProperties();
                break;
            case GsmtcCommand::RefreshTimeline:
                HandleRefreshTimeline();
                break;
            case GsmtcCommand::TogglePlayPause:
                if (m_currentSession) {
                    m_currentSession.TryTogglePlayPauseAsync();
                }
                break;
            case GsmtcCommand::Play:
                if (m_currentSession) {
                    m_currentSession.TryPlayAsync();
                }
                break;
            case GsmtcCommand::Pause:
                if (m_currentSession) {
                    m_currentSession.TryPauseAsync();
                }
                break;
            case GsmtcCommand::Next:
                if (m_currentSession) {
                    m_currentSession.TrySkipNextAsync();
                }
                break;
            case GsmtcCommand::Previous:
                if (m_currentSession) {
                    m_currentSession.TrySkipPreviousAsync();
                }
                break;
            default:
                break;
        }
    }
};

GsmtcMediaSource::GsmtcMediaSource() : m_impl(std::make_unique<Impl>()) {}

GsmtcMediaSource::~GsmtcMediaSource() = default;

bool GsmtcMediaSource::Initialize() {
    return m_impl ? m_impl->Initialize() : false;
}

void GsmtcMediaSource::Shutdown() {
    if (m_impl) {
        m_impl->Shutdown();
    }
}

bool GsmtcMediaSource::HasNewState() const {
    return m_impl ? m_impl->m_bridge.HasPendingChange() : false;
}

bool GsmtcMediaSource::GetCurrentSnapshot(TEMediaStateSnapshot* outSnapshot) {
    return m_impl ? m_impl->m_bridge.ConsumeSnapshot(outSnapshot) : false;
}

void GsmtcMediaSource::TogglePlayPauseAsync() {
    if (m_impl) {
        m_impl->PostCommand(GsmtcCommand::TogglePlayPause);
    }
}

void GsmtcMediaSource::PlayAsync() {
    if (m_impl) {
        m_impl->PostCommand(GsmtcCommand::Play);
    }
}

void GsmtcMediaSource::PauseAsync() {
    if (m_impl) {
        m_impl->PostCommand(GsmtcCommand::Pause);
    }
}

void GsmtcMediaSource::NextAsync() {
    if (m_impl) {
        m_impl->PostCommand(GsmtcCommand::Next);
    }
}

void GsmtcMediaSource::PreviousAsync() {
    if (m_impl) {
        m_impl->PostCommand(GsmtcCommand::Previous);
    }
}

bool GsmtcMediaSource::IsRunning() const {
    return m_impl ? m_impl->m_running.load() : false;
}

void GsmtcMediaSource::SetWakeCallback(void (*callback)(void)) {
    if (m_impl) {
        m_impl->m_bridge.SetWakeCallback(callback);
    }
}

std::unique_ptr<IMediaSource> CreateGsmtcMediaSource() {
    return std::make_unique<GsmtcMediaSource>();
}
