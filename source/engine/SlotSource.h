#pragma once

// The one interface every audio source implements (CLAUDE.md "Engine boundaries").
// The engine never knows whether a slot is a file, a native bus or a Send.

#include <cstdint>

namespace mosaic::engine
{

class SlotSource
{
public:
    virtual ~SlotSource() = default;

    /// True if nothing can be played from this slot. Empty slots are never chosen.
    virtual bool isEmpty() const noexcept = 0;

    /// True for live sources: audio exists only for the timeline positions of the
    /// current block, and "source position" means timeline position.
    virtual bool isLive() const noexcept = 0;

    /// Length in samples for file sources (used for Loop and Free). 0 for live sources.
    virtual int64_t lengthInSamples() const noexcept = 0;

    /// Fill n samples starting at source position pos. Positions outside the source
    /// must produce silence; this is never called with n > the engine's prepared block.
    /// Audio-thread safe: no allocation, no locks.
    virtual void read (int64_t pos, float* left, float* right, int n) noexcept = 0;
};

} // namespace mosaic::engine
