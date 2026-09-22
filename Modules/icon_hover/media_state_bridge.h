#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cstring>
#include "dynamic_island_media.h"

// High-performance double-buffered state bridge connecting background media threads
// with the UI frame loop render thread via an SRWLock and atomic dirty flag.
// - Background thread publishes with exclusive lock.
// - UI / frame loop thread consumes with shared lock only when dirty flag is true (fast path < 50 ns, zero allocations).
class MediaStateBridge {
public:
    MediaStateBridge() {
        InitializeSRWLock(&m_lock);
        std::memset(&m_buffers[0], 0, sizeof(m_buffers[0]));
        std::memset(&m_buffers[1], 0, sizeof(m_buffers[1]));
    }

    ~MediaStateBridge() = default;

    // Non-copyable, non-movable synchronization primitive
    MediaStateBridge(const MediaStateBridge&) = delete;
    MediaStateBridge& operator=(const MediaStateBridge&) = delete;
    MediaStateBridge(MediaStateBridge&&) = delete;
    MediaStateBridge& operator=(MediaStateBridge&&) = delete;

    void SetWakeCallback(void (*callback)(void)) {
        m_wakeCallback = callback;
    }

    // Called by background worker thread (Exclusive Publish)
    void PublishSnapshot(const TEMediaStateSnapshot& newSnapshot, bool wakeAnimation = true) {
        AcquireSRWLockExclusive(&m_lock);
        const int writeIndex = 1 - m_readIndex.load(std::memory_order_relaxed);
        m_buffers[writeIndex] = newSnapshot;
        m_readIndex.store(writeIndex, std::memory_order_release);
        m_dirty.store(true, std::memory_order_release);
        ReleaseSRWLockExclusive(&m_lock);

        if (wakeAnimation && m_wakeCallback) {
            m_wakeCallback();
        }
    }

    // Called ONLY by UI / frame loop thread (Fast path: < 50 ns, Shared Consume)
    // Returns true if a dirty snapshot was consumed, false if state is unchanged.
    bool ConsumeSnapshot(TEMediaStateSnapshot* outSnapshot) {
        // Fast-path lockless check: if no new data, exit immediately
        if (!m_dirty.load(std::memory_order_acquire)) {
            return false;
        }

        AcquireSRWLockShared(&m_lock);
        const int readIndex = m_readIndex.load(std::memory_order_acquire);
        if (outSnapshot != nullptr) {
            *outSnapshot = m_buffers[readIndex];
        }
        m_dirty.store(false, std::memory_order_release);
        ReleaseSRWLockShared(&m_lock);
        return true;
    }

    // Query pending change status without consuming
    bool HasPendingChange() const {
        return m_dirty.load(std::memory_order_acquire);
    }

    // Read the current active snapshot without consuming the dirty flag
    bool GetLatestSnapshot(TEMediaStateSnapshot* outSnapshot) const {
        if (outSnapshot == nullptr) {
            return false;
        }
        AcquireSRWLockShared(const_cast<PSRWLOCK>(&m_lock));
        const int readIndex = m_readIndex.load(std::memory_order_acquire);
        *outSnapshot = m_buffers[readIndex];
        ReleaseSRWLockShared(const_cast<PSRWLOCK>(&m_lock));
        return true;
    }

    // Reset bridge state (e.g. on session shutdown)
    void Reset() {
        AcquireSRWLockExclusive(&m_lock);
        std::memset(&m_buffers[0], 0, sizeof(m_buffers[0]));
        std::memset(&m_buffers[1], 0, sizeof(m_buffers[1]));
        m_readIndex.store(0, std::memory_order_release);
        m_dirty.store(false, std::memory_order_release);
        ReleaseSRWLockExclusive(&m_lock);
    }

private:
    mutable SRWLOCK m_lock;
    TEMediaStateSnapshot m_buffers[2];
    std::atomic<int> m_readIndex{0};
    std::atomic<bool> m_dirty{false};
    void (*m_wakeCallback)(void){nullptr};
};
