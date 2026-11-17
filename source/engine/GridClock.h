#pragma once

// Maps host time (ppq, bpm, sample rate) to step indices and sample-exact step
// boundaries (SPEC 4.2). Stateless: every function is a pure function of its inputs,
// so any split of the timeline into blocks gives the same boundaries.

#include <cstdint>
#include "engine/Rate.h"

namespace mosaic::engine
{

struct HostBlock
{
    double  ppqAtStart    = 0.0;   ///< musical position of sample 0 of this block
    double  bpm           = 120.0;
    double  sampleRate    = 48000.0;
    int64_t timeInSamples = 0;     ///< timeline position of sample 0 of this block
    bool    playing       = false;

    bool isValid() const noexcept { return bpm > 0.0 && sampleRate > 0.0; }
};

class GridClock
{
public:
    static double samplesPerQuarter (const HostBlock& b) noexcept { return b.sampleRate * 60.0 / b.bpm; }

    /// Sample offset (relative to the block start, may be negative or beyond the block)
    /// at which step k begins. Boundaries that fall within 1e-6 samples of an integer
    /// snap to it, so the same boundary computed from different blocks agrees.
    static int64_t boundaryOffset (const HostBlock& b, double stepQN, int64_t k) noexcept;

    /// The step that contains sample 0 of the block.
    static int64_t stepAtStart (const HostBlock& b, double stepQN) noexcept;

    /// Length in samples of step k (always >= 1).
    static int64_t stepLengthSamples (const HostBlock& b, double stepQN, int64_t k) noexcept;

    /// Absolute timeline sample at which step k begins.
    static int64_t boundaryTimeline (const HostBlock& b, double stepQN, int64_t k) noexcept
    {
        return b.timeInSamples + boundaryOffset (b, stepQN, k);
    }

    /// Advance a block description by n samples (used when a render is chunked).
    static HostBlock advanced (const HostBlock& b, int n) noexcept;

    static constexpr double kSnapEpsilonSamples = 1e-6;
};

} // namespace mosaic::engine
