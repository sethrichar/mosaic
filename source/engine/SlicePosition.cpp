#include "engine/SlicePosition.h"
#include "engine/Hash.h"

#include <algorithm>

namespace mosaic::engine
{

namespace
{
    constexpr uint64_t kFreeSalt = 0x46726565ULL; // "Free"

    inline int clampRun (int64_t v, int maxRun) noexcept
    {
        return (int) std::clamp<int64_t> (v, 1, (int64_t) maxRun);
    }
}

SourceRun SlicePosition::locked (const SlotSource& src, const SlotPlacement& place, int64_t timelinePos, int maxRun) noexcept
{
    if (src.isLive())
        return { timelinePos, maxRun, false };

    const int64_t len = src.lengthInSamples();
    if (len <= 0)
        return { 0, maxRun, true };

    int64_t p = timelinePos - place.startOffsetSamples;

    if (place.loop)
    {
        p = floorMod (p, len);
        return { p, clampRun (len - p, maxRun), false };
    }

    if (p < 0)   return { 0, clampRun (-p, maxRun), true };
    if (p >= len) return { 0, maxRun, true };
    return { p, clampRun (len - p, maxRun), false };
}

int64_t SlicePosition::freeSliceIndex (int64_t fileLength, int64_t stepLengthSamples,
                                       int64_t stepIndex, int slot, uint32_t seed) noexcept
{
    if (stepLengthSamples <= 0) return 0;
    const int64_t numSlices = std::max<int64_t> (1, fileLength / stepLengthSamples);
    return (int64_t) (hash (seed, (uint64_t) stepIndex, (uint64_t) slot, kFreeSalt) % (uint64_t) numSlices);
}

SourceRun SlicePosition::free (const SlotSource& src, const SlotPlacement& place, int64_t timelinePos, int64_t tileStartPos,
                               int64_t stepIndex, int slot, uint32_t seed, int64_t stepLengthSamples, int maxRun) noexcept
{
    if (src.isLive())
        return locked (src, place, timelinePos, maxRun);

    const int64_t len = src.lengthInSamples();
    if (len <= 0)
        return { 0, maxRun, true };

    const int64_t slice = freeSliceIndex (len, stepLengthSamples, stepIndex, slot, seed);
    const int64_t d = timelinePos - tileStartPos;          // >= 0 by construction
    const int64_t p = slice * stepLengthSamples + d;

    if (p < 0 || p >= len)
        return { 0, maxRun, true };
    return { p, clampRun (len - p, maxRun), false };
}

} // namespace mosaic::engine
