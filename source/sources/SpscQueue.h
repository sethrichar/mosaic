#pragma once

// Fixed-capacity single-producer / single-consumer queue of trivially copyable values.
// Lock-free, allocation-free. Used to hand buffers between the message thread and the
// audio thread (CLAUDE.md real-time rules). Plain C++.

#include <array>
#include <atomic>
#include <cstddef>

namespace mosaic::sources
{

template <typename T, size_t Capacity>
class SpscQueue
{
public:
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    /// Producer. Returns false if the queue is full (the caller keeps the item).
    bool push (const T& item) noexcept
    {
        const size_t w = writePos.load (std::memory_order_relaxed);
        const size_t r = readPos.load (std::memory_order_acquire);
        if (w - r >= Capacity) return false;
        slots[w & kMask] = item;
        writePos.store (w + 1, std::memory_order_release);
        return true;
    }

    /// Consumer. Returns false if the queue is empty.
    bool pop (T& out) noexcept
    {
        const size_t r = readPos.load (std::memory_order_relaxed);
        const size_t w = writePos.load (std::memory_order_acquire);
        if (r == w) return false;
        out = slots[r & kMask];
        readPos.store (r + 1, std::memory_order_release);
        return true;
    }

    bool empty() const noexcept
    {
        return readPos.load (std::memory_order_acquire) == writePos.load (std::memory_order_acquire);
    }

private:
    static constexpr size_t kMask = Capacity - 1;
    std::array<T, Capacity> slots {};
    std::atomic<size_t> readPos { 0 }, writePos { 0 };
};

} // namespace mosaic::sources
