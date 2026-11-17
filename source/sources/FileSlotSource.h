#pragma once

// SlotSource for a loaded file (SPEC section 6). The audio thread owns the "current"
// buffer outright; new buffers arrive through a lock-free queue and old ones go back
// through another, so nothing is allocated or freed on the audio thread and no pointer
// is ever shared between threads. Plain C++ (tested without JUCE).

#include <memory>
#include <vector>

#include "engine/SlotSource.h"
#include "sources/LoadedBuffer.h"
#include "sources/SpscQueue.h"

namespace mosaic::sources
{

class FileSlotSource final : public engine::SlotSource
{
public:
    FileSlotSource() = default;
    ~FileSlotSource() override;

    //==========================================================================
    // Audio thread

    /// Call once at the start of every block: takes delivery of a new buffer if one
    /// arrived and retires the old one. Swaps happen only between blocks.
    void pollIncoming() noexcept;

    /// True if a new buffer is waiting (audio thread may defer the swap to a tile boundary).
    bool hasIncoming() const noexcept { return ! incoming.empty(); }

    bool    isEmpty() const noexcept override         { return current == nullptr || current->numSamples <= 0; }
    bool    isLive() const noexcept override          { return false; }
    int64_t lengthInSamples() const noexcept override { return current != nullptr ? current->numSamples : 0; }
    void    read (int64_t pos, float* left, float* right, int n) noexcept override;

    //==========================================================================
    // Message thread

    /// Hands a new buffer to the audio thread (nullptr clears the slot). Ownership moves.
    void publish (std::unique_ptr<LoadedBuffer> buffer);

    /// Frees buffers the audio thread has finished with, and retries any publish that
    /// found the queue full. Call regularly (a timer).
    void collectGarbage();

    /// Total bytes held by this slot across all buffers it still owns (for the readout).
    size_t bytesInUse() const noexcept;

private:
    static constexpr size_t kQueueSize = 8;

    LoadedBuffer* current = nullptr;                                     // audio thread only
    SpscQueue<LoadedBuffer*, kQueueSize> incoming;                       // message -> audio
    SpscQueue<LoadedBuffer*, kQueueSize> retired;                        // audio -> message

    std::vector<std::unique_ptr<LoadedBuffer>> pendingPublish;           // message thread only
    std::vector<std::unique_ptr<LoadedBuffer>> inFlight;                 // message thread: owned until retired
};

} // namespace mosaic::sources
