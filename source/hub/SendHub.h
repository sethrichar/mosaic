#pragma once

// Mosaic Send hub — process-wide, lock-free audio transport between plugin instances.
// Plain C++ (no JUCE) so it can be unit-tested on its own. See SPEC section 5.3.
//
// One SendChannel per (Group, Send number). A Send-role instance is the single producer;
// the Mosaic-role instance in the same Group is the consumer. Every write is tagged with
// the producer's timeline sample position, and the consumer asks for audio *by timeline
// position*, so host processing order does not matter as long as the consumer reads far
// enough behind the producer (the look-behind L, reported to the host as latency).

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace mosaic::hub
{

inline constexpr int     kNumGroups = 4;
inline constexpr int     kNumSends  = 8;
inline constexpr int     kRingSize  = 1 << 19;   // 524288 samples > 2 s at 192 kHz; power of two
inline constexpr int64_t kNoHead    = INT64_MIN; // "nothing has ever been written"

class SendChannel
{
public:
    SendChannel() = default;
    SendChannel (const SendChannel&) = delete;
    SendChannel& operator= (const SendChannel&) = delete;

    /// Message thread only. Allocates the ring once; it is never freed. Idempotent.
    void allocate();
    bool isAllocated() const noexcept { return ringL.load (std::memory_order_acquire) != nullptr; }

    //==========================================================================
    // Producer side (audio thread of the Send-role instance)

    /// Writes n samples whose first sample sits at timeline position `pos`.
    /// Returns false if nothing was written (unallocated, or n out of range).
    bool write (int producerId, int64_t pos, const float* left, const float* right, int n) noexcept;

    /// Called when a producer stops using this channel (role change, destruction).
    void releaseOwnership (int producerId) noexcept;

    //==========================================================================
    // Consumer side (audio thread of the Mosaic-role instance)

    struct ReadResult
    {
        bool    valid;  ///< true if every requested sample was available
        int64_t head;   ///< producer's head (one past the newest written sample), or kNoHead
    };

    /// Copies the n samples starting at timeline `pos` into left/right.
    /// Fills silence and returns valid == false if any part is unavailable: never
    /// written, older than the ring holds, or before the last discontinuity (locate,
    /// loop wrap) — stale audio is never returned.
    ReadResult read (int64_t pos, float* left, float* right, int n) const noexcept;

    /// Peak absolute sample over the newest n samples (for meters). 0 if nothing written.
    float peakOfLatest (int n) const noexcept;

    int64_t  head()           const noexcept { return headPos.load (std::memory_order_acquire); }
    /// Oldest timeline position that can still be read (contiguous with the head), or kNoHead.
    int64_t  tail()           const noexcept { return tailPos.load (std::memory_order_acquire); }
    int      owner()          const noexcept { return ownerId.load (std::memory_order_relaxed); }
    uint64_t collisionCount() const noexcept { return collisions.load (std::memory_order_relaxed); }

private:
    std::unique_ptr<float[]> storageL, storageR;
    std::atomic<float*>      ringL { nullptr }, ringR { nullptr };
    std::atomic<int64_t>     headPos { kNoHead };
    std::atomic<int64_t>     tailPos { kNoHead };
    std::atomic<int>         ownerId { 0 };
    std::atomic<uint64_t>    collisions { 0 };
};

class SendHub
{
public:
    /// The one hub shared by every Mosaic instance loaded in this process.
    static SendHub& instance();

    static constexpr int index (int group, int send) noexcept { return group * kNumSends + send; }

    SendChannel& channel (int group, int send) noexcept { return channels[(size_t) index (group, send)]; }
    SendChannel& channelAt (int idx) noexcept            { return channels[(size_t) idx]; }

    /// Unique id per plugin instance, used for collision detection. Never 0.
    int newInstanceId() noexcept { return nextId.fetch_add (1, std::memory_order_relaxed); }

private:
    SendHub() = default;
    std::array<SendChannel, kNumGroups * kNumSends> channels;
    std::atomic<int> nextId { 1 };
};

} // namespace mosaic::hub
