#include "engine/TileRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mosaic::engine
{

void TileRenderer::prepare (int maxBlockSize)
{
    capacity = std::max (64, maxBlockSize);
    scratchL.assign ((size_t) capacity, 0.0f);
    scratchR.assign ((size_t) capacity, 0.0f);
}

void TileRenderer::render (const HostBlock& host, const RenderParams& p, const ChooserConfig& c,
                           SlotSource* const slots[kNumSlots], float* outL, float* outR, int n) noexcept
{
    if (n <= 0) return;
    std::memset (outL, 0, (size_t) n * sizeof (float));
    std::memset (outR, 0, (size_t) n * sizeof (float));

    if (! host.playing || ! host.isValid() || capacity <= 0)
        return;

    // Chunk so no read exceeds the scratch buffers (handles blocks larger than prepared).
    HostBlock h = host;
    int done = 0;
    while (done < n)
    {
        const int chunk = std::min (capacity, n - done);
        renderChunk (h, p, c, slots, outL + done, outR + done, chunk);
        h = GridClock::advanced (h, chunk);
        done += chunk;
    }
}

void TileRenderer::renderChunk (const HostBlock& host, const RenderParams& p, const ChooserConfig& c,
                                SlotSource* const slots[kNumSlots], float* outL, float* outR, int n) noexcept
{
    const double stepQN = p.rate.lengthInQuarterNotes();
    const int64_t fadeSamples = (int64_t) std::llround (std::max (0.0, p.fadeMs) * host.sampleRate / 1000.0);
    const bool gated = p.tileLengthPercent < 100.0;

    int64_t k      = GridClock::stepAtStart (host, stepQN);
    int64_t startK = GridClock::boundaryOffset (host, stepQN, k);       // <= 0
    int64_t next   = GridClock::boundaryOffset (host, stepQN, k + 1);   // > 0

    int pos = 0;
    while (pos < n)
    {
        const int segEnd = (int) std::min<int64_t> (n, next);
        const int64_t lenK    = std::max<int64_t> (1, next - startK);
        const int64_t startP  = GridClock::boundaryOffset (host, stepQN, k - 1);
        const int64_t lenPrev = std::max<int64_t> (1, startK - startP);

        const int slotK    = StepChooser::choose (c, k);
        const int slotPrev = gated ? StepChooser::kNone : StepChooser::choose (c, k - 1);

        // Crossfade at this boundary: never longer than half of either neighbouring step.
        const int64_t fadeK = std::min ({ fadeSamples, lenK / 2, lenPrev / 2 });

        // Same slot twice in Locked mode is one continuous signal: no fade needed.
        const bool seamless = ! gated && ! p.freePosition && slotK != StepChooser::kNone && slotK == slotPrev;

        if (slotK != StepChooser::kNone && slots[slotK] != nullptr && ! slots[slotK]->isEmpty())
            addTile (host, p, *slots[slotK], slotK, k, host.timeInSamples + startK, lenK, fadeK, ! seamless,
                     Edge::Incoming, pos, segEnd, outL, outR);

        if (! seamless && slotPrev != StepChooser::kNone && slots[slotPrev] != nullptr && ! slots[slotPrev]->isEmpty())
        {
            const int outEnd = (int) std::min<int64_t> (segEnd, startK + fadeK);
            if (outEnd > pos)
                addTile (host, p, *slots[slotPrev], slotPrev, k - 1, host.timeInSamples + startP, lenPrev, fadeK, false,
                         Edge::Outgoing, pos, outEnd, outL, outR);
        }

        pos = segEnd;
        ++k;
        startK = next;
        next = GridClock::boundaryOffset (host, stepQN, k + 1);
    }
}

void TileRenderer::addTile (const HostBlock& host, const RenderParams& p, SlotSource& src, int slot, int64_t step,
                            int64_t tileStartTimeline, int64_t stepLength, int64_t fadeAtStart, bool fadeInEnabled,
                            Edge edge, int from, int to, float* outL, float* outR) noexcept
{
    const bool gated = p.tileLengthPercent < 100.0;
    const int64_t tileLen = gated
        ? std::max<int64_t> (1, (int64_t) std::llround ((double) stepLength * std::clamp (p.tileLengthPercent, 0.0, 100.0) / 100.0))
        : stepLength;
    const int64_t fadeSamples = (int64_t) std::llround (std::max (0.0, p.fadeMs) * host.sampleRate / 1000.0);
    const int64_t gateFade = gated ? std::min (fadeSamples, tileLen / 2) : 0;

    const float gain = p.slotGain[slot];
    int cursor = from;

    while (cursor < to)
    {
        const int64_t timeline = host.timeInSamples + cursor;
        const int64_t d = timeline - tileStartTimeline;     // distance into the tile's step

        // Where this tile is audible.
        if (edge == Edge::Incoming && d >= tileLen)
            return;                                         // past the gate: silence
        if (edge == Edge::Outgoing && d >= stepLength + fadeAtStart)
            return;

        int runMax = to - cursor;
        if (edge == Edge::Incoming) runMax = (int) std::min<int64_t> (runMax, tileLen - d);
        if (edge == Edge::Outgoing) runMax = (int) std::min<int64_t> (runMax, stepLength + fadeAtStart - d);
        runMax = std::min (runMax, capacity);
        if (runMax <= 0) return;

        const SourceRun run = p.freePosition
            ? SlicePosition::free (src, p.placement[slot], timeline, tileStartTimeline, step, slot, p.seed, stepLength, runMax)
            : SlicePosition::locked (src, p.placement[slot], timeline, runMax);
        const int len = std::clamp (run.length, 1, runMax);

        if (! run.silent)
        {
            src.read (run.sourcePos, scratchL.data(), scratchR.data(), len);

            for (int i = 0; i < len; ++i)
            {
                const int64_t di = d + i;
                float env = gain;

                if (edge == Edge::Incoming)
                {
                    if (fadeInEnabled && fadeAtStart > 0 && di < fadeAtStart)
                        env *= (float) di / (float) fadeAtStart;
                    if (gated && gateFade > 0 && di >= tileLen - gateFade)
                        env *= (float) (tileLen - di) / (float) gateFade;
                }
                else // Outgoing: fade out over [stepLength, stepLength + fadeAtStart)
                {
                    const int64_t o = di - stepLength;
                    env *= (o < 0) ? 1.0f : (float) (fadeAtStart - o) / (float) fadeAtStart;
                }

                outL[cursor + i] += scratchL[(size_t) i] * env;
                outR[cursor + i] += scratchR[(size_t) i] * env;
            }
        }

        cursor += len;
    }
}

} // namespace mosaic::engine
