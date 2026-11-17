#include "hub/SendHub.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mosaic::hub
{

namespace
{
    constexpr size_t kMask = (size_t) kRingSize - 1;

    inline size_t slot (int64_t pos) noexcept
    {
        // Two's-complement cast keeps consecutive positions in consecutive slots,
        // including across negative (pre-roll) positions.
        return (size_t) pos & kMask;
    }

    // Copies n samples between a linear buffer and the ring, in up to two segments.
    inline void writeRing (float* ring, const float* linear, int64_t pos, int n) noexcept
    {
        const size_t start = slot (pos);
        const int first = (int) std::min<size_t> ((size_t) n, (size_t) kRingSize - start);
        const int rest  = n - first;
        std::memcpy (ring + start, linear, (size_t) first * sizeof (float));
        if (rest > 0) std::memcpy (ring, linear + first, (size_t) rest * sizeof (float));
    }

    inline void readRing (const float* ring, float* linear, int64_t pos, int n) noexcept
    {
        const size_t start = slot (pos);
        const int first = (int) std::min<size_t> ((size_t) n, (size_t) kRingSize - start);
        const int rest  = n - first;
        std::memcpy (linear, ring + start, (size_t) first * sizeof (float));
        if (rest > 0) std::memcpy (linear + first, ring, (size_t) rest * sizeof (float));
    }

    inline void silence (float* left, float* right, int n) noexcept
    {
        if (n <= 0) return;
        std::memset (left,  0, (size_t) n * sizeof (float));
        std::memset (right, 0, (size_t) n * sizeof (float));
    }
}

void SendChannel::allocate()
{
    if (isAllocated())
        return;

    storageL = std::make_unique<float[]> ((size_t) kRingSize);   // zero-initialised
    storageR = std::make_unique<float[]> ((size_t) kRingSize);
    ringR.store (storageR.get(), std::memory_order_release);
    ringL.store (storageL.get(), std::memory_order_release);     // published last: readers check ringL
}

bool SendChannel::write (int producerId, int64_t pos, const float* left, const float* right, int n) noexcept
{
    float* L = ringL.load (std::memory_order_acquire);
    if (L == nullptr || n <= 0 || n > kRingSize)
        return false;
    float* R = ringR.load (std::memory_order_relaxed);

    const int prev = ownerId.exchange (producerId, std::memory_order_relaxed);
    if (prev != 0 && prev != producerId)
        collisions.fetch_add (1, std::memory_order_relaxed);

    writeRing (L, left,  pos, n);
    writeRing (R, right, pos, n);

    // Valid region is [tail, head). A discontinuity (first write, locate, loop wrap)
    // restarts it at pos; otherwise it just loses what the ring can no longer hold.
    const int64_t prevHead = headPos.load (std::memory_order_relaxed);
    int64_t tail = tailPos.load (std::memory_order_relaxed);
    if (prevHead == kNoHead || pos != prevHead)
        tail = pos;
    tail = std::max (tail, pos + n - kRingSize);
    tailPos.store (tail, std::memory_order_release);
    headPos.store (pos + n, std::memory_order_release);
    return true;
}

void SendChannel::releaseOwnership (int producerId) noexcept
{
    int expected = producerId;
    ownerId.compare_exchange_strong (expected, 0, std::memory_order_relaxed);
}

SendChannel::ReadResult SendChannel::read (int64_t pos, float* left, float* right, int n) const noexcept
{
    const float* L = ringL.load (std::memory_order_acquire);
    const int64_t headBefore = headPos.load (std::memory_order_acquire);
    const int64_t tailBefore = tailPos.load (std::memory_order_acquire);

    if (L == nullptr || n <= 0 || n > kRingSize || headBefore == kNoHead
        || pos + n > headBefore || pos < tailBefore)
    {
        silence (left, right, n);
        return { false, headBefore };
    }

    const float* R = ringR.load (std::memory_order_relaxed);
    readRing (L, left,  pos, n);
    readRing (R, right, pos, n);

    // If the producer moved the valid region while we were copying, the data is torn.
    const int64_t headAfter = headPos.load (std::memory_order_acquire);
    const int64_t tailAfter = tailPos.load (std::memory_order_acquire);
    if (pos < tailAfter || pos + n > headAfter)
    {
        silence (left, right, n);
        return { false, headAfter };
    }
    return { true, headAfter };
}

float SendChannel::peakOfLatest (int n) const noexcept
{
    const float* L = ringL.load (std::memory_order_acquire);
    const int64_t h = headPos.load (std::memory_order_acquire);
    if (L == nullptr || h == kNoHead || n <= 0)
        return 0.0f;
    const float* R = ringR.load (std::memory_order_relaxed);

    const int64_t t = tailPos.load (std::memory_order_acquire);
    n = (int) std::min<int64_t> (n, h - t);
    float peak = 0.0f;
    for (int64_t p = h - n; p < h; ++p)
    {
        const size_t s = slot (p);
        peak = std::max ({ peak, std::fabs (L[s]), std::fabs (R[s]) });
    }
    return peak;
}

SendHub& SendHub::instance()
{
    static SendHub hub;
    return hub;
}

} // namespace mosaic::hub
