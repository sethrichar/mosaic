#pragma once

// Where a tile's audio is taken from within its source (SPEC 4.4): Locked or Free.
// Pure functions. A "run" is the longest contiguous stretch readable from one source
// position, so the renderer can read with few calls and never straddle a loop wrap.

#include <cstdint>
#include "engine/SlotSource.h"

namespace mosaic::engine
{

struct SlotPlacement
{
    int64_t startOffsetSamples = 0;   ///< slides the file against the timeline (Locked)
    bool    loop               = true;
};

struct SourceRun
{
    int64_t sourcePos;   ///< where to read from (ignored when silent)
    int     length;      ///< > 0, <= maxRun
    bool    silent;
};

class SlicePosition
{
public:
    /// Locked: the source runs in sync with the timeline (file position = timeline −
    /// start offset, wrapped if Loop is on; silence outside the file if it is off).
    /// Live sources always read "now" (source position == timeline position).
    static SourceRun locked (const SlotSource&, const SlotPlacement&, int64_t timelinePos, int maxRun) noexcept;

    /// Free: the file is cut into slices of the step length from its start; the slice
    /// for this tile is a pure function of (seed, stepIndex, slot). Live sources fall
    /// back to Locked.
    static SourceRun free (const SlotSource&, const SlotPlacement&, int64_t timelinePos, int64_t tileStartPos,
                           int64_t stepIndex, int slot, uint32_t seed, int64_t stepLengthSamples, int maxRun) noexcept;

    /// The slice index Free mode picks (exposed for tests and the UI).
    static int64_t freeSliceIndex (int64_t fileLength, int64_t stepLengthSamples,
                                   int64_t stepIndex, int slot, uint32_t seed) noexcept;
};

} // namespace mosaic::engine
