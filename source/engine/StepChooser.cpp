#include "engine/StepChooser.h"
#include "engine/Hash.h"

namespace mosaic::engine
{

namespace
{
    constexpr uint64_t kPickSalt   = 0x5069636bULL;   // "Pick"
    constexpr uint64_t kRepickSalt = 0x52657069ULL;   // "Repi"

    // Weighted pick among candidate slots (available && weight > 0), optionally
    // excluding one slot. Returns kNone if there is nothing to pick from.
    int weightedPick (const ChooserConfig& c, int64_t step, uint64_t salt, int exclude) noexcept
    {
        double total = 0.0;
        for (int s = 0; s < kNumSlots; ++s)
            if (c.available[s] && c.weight[s] > 0.0f && s != exclude)
                total += (double) c.weight[s];

        if (total <= 0.0)
            return StepChooser::kNone;

        const double r = unitFloat (hash (c.seed, (uint64_t) step, salt, (uint64_t) (exclude + 1))) * total;
        double acc = 0.0;
        int last = StepChooser::kNone;
        for (int s = 0; s < kNumSlots; ++s)
        {
            if (! (c.available[s] && c.weight[s] > 0.0f && s != exclude))
                continue;
            acc += (double) c.weight[s];
            last = s;
            if (r < acc)
                return s;
        }
        return last;   // floating-point edge: r == total
    }

    int numCandidates (const ChooserConfig& c) noexcept
    {
        int n = 0;
        for (int s = 0; s < kNumSlots; ++s)
            if (c.available[s] && c.weight[s] > 0.0f) ++n;
        return n;
    }
}

int StepChooser::chooseRandom (const ChooserConfig& c, int64_t stepIndex) noexcept
{
    const int64_t step = c.patternLengthSteps > 0 ? floorMod (stepIndex, c.patternLengthSteps) : stepIndex;

    if (! c.avoidRepeats || numCandidates (c) < 2)
        return weightedPick (c, step, kPickSalt, kNone);

    // Stateless "no two in a row": walk the chain from its last restart point. If the
    // pattern loops, walk within the pattern so the loop point stays consistent.
    const int64_t chainStart = floorDiv (step, kChainLength) * kChainLength;
    int prev = kNone;
    for (int64_t s = chainStart; s <= step; ++s)
    {
        int pick = weightedPick (c, s, kPickSalt, kNone);
        if (pick != kNone && pick == prev)
            pick = weightedPick (c, s, kRepickSalt, prev);
        prev = pick;
    }
    return prev;
}

int StepChooser::choose (const ChooserConfig& c, int64_t stepIndex) noexcept
{
    if (c.mode == ChooseMode::Random)
        return chooseRandom (c, stepIndex);

    const int len = c.sequenceLength < 1 ? 1 : (c.sequenceLength > ChooserConfig::kMaxSequenceSteps
                                                    ? ChooserConfig::kMaxSequenceSteps : c.sequenceLength);
    const int8_t v = c.sequence[floorMod (stepIndex, len)];

    if (v == ChooserConfig::kRandom) return chooseRandom (c, stepIndex);
    if (v < 0 || v >= kNumSlots)     return kNone;                       // rest
    return (c.available[v] && c.weight[v] >= 0.0f) ? (int) v : kNone;    // muted/empty slot acts as a rest
}

} // namespace mosaic::engine
