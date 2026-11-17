#pragma once

// Renders the mosaic: one tile at a time, fades at every tile edge, optional gate
// (SPEC 4.4–4.6). Stateless with respect to the timeline: the output for any sample is a
// pure function of host time and configuration, so any block split, locate or offline
// bounce renders bit-identically (SPEC section 11).

#include <cstdint>
#include <vector>

#include "engine/GridClock.h"
#include "engine/SlicePosition.h"
#include "engine/SlotSource.h"
#include "engine/StepChooser.h"

namespace mosaic::engine
{

struct RenderParams
{
    Rate   rate;
    bool   freePosition      = false;   ///< SPEC 4.4: Locked (false) or Free (true)
    double fadeMs            = 5.0;     ///< 0–20 ms declick fade at every tile edge
    double tileLengthPercent = 100.0;   ///< 10–100: gate; below 100 leaves silence before the next tile
    uint32_t seed            = 0;       ///< also used by the chooser (keep in sync)

    float         slotGain[kNumSlots]  = { 1, 1, 1, 1, 1, 1, 1, 1 };   ///< linear
    SlotPlacement placement[kNumSlots] = {};
};

class TileRenderer
{
public:
    /// Allocates scratch for blocks up to maxBlockSize. Larger renders are chunked.
    void prepare (int maxBlockSize);

    /// Renders n samples of the mosaic into outL/outR (overwrites). Silence when the
    /// transport is stopped or the host block is invalid. Audio-thread safe after prepare().
    void render (const HostBlock&, const RenderParams&, const ChooserConfig&,
                 SlotSource* const slots[kNumSlots], float* outL, float* outR, int n) noexcept;

    /// Diagnostics for the UI/strip: which slot plays step k, and its timeline start.
    static int  slotForStep (const ChooserConfig& c, int64_t step) noexcept { return StepChooser::choose (c, step); }

private:
    enum class Edge { Incoming, Outgoing };

    void renderChunk (const HostBlock&, const RenderParams&, const ChooserConfig&,
                      SlotSource* const slots[kNumSlots], float* outL, float* outR, int n) noexcept;

    void addTile (const HostBlock&, const RenderParams&, SlotSource& src, int slot, int64_t step,
                  int64_t tileStartTimeline, int64_t stepLength, int64_t fadeAtStart, bool fadeInEnabled,
                  Edge edge, int from, int to, float* outL, float* outR) noexcept;

    std::vector<float> scratchL, scratchR;
    int capacity = 0;
};

} // namespace mosaic::engine
