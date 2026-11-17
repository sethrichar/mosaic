#pragma once

// Chooses which slot plays on a given step (SPEC 4.3). Stateless: the result is a pure
// function of the configuration and the step index, so bar 5 sounds the same whether
// you played from bar 1 or located straight to it.

#include <cstdint>
#include "engine/EngineInfo.h"   // kNumSlots

namespace mosaic::engine
{

enum class ChooseMode : int { Random = 0, Sequence };

struct ChooserConfig
{
    ChooseMode mode = ChooseMode::Random;
    uint32_t   seed = 0;
    int        patternLengthSteps = 0;   ///< 0 = Off (never repeats); else the random pattern loops every N steps
    bool       avoidRepeats = false;

    float      weight[kNumSlots]    = { 100, 100, 100, 100, 100, 100, 100, 100 };   ///< 0–100
    bool       available[kNumSlots] = { false, false, false, false, false, false, false, false }; ///< not empty and not muted

    static constexpr int    kMaxSequenceSteps = 64;
    static constexpr int8_t kRest   = -1;
    static constexpr int8_t kRandom = -2;   ///< "?" — pick randomly for this step
    int    sequenceLength = 16;
    int8_t sequence[kMaxSequenceSteps] = { 0 };
};

class StepChooser
{
public:
    static constexpr int kNone = -1;   ///< rest / nothing to play

    /// Slot index 0–7 to play on this step, or kNone.
    static int choose (const ChooserConfig&, int64_t stepIndex) noexcept;

    /// How far back the avoid-repeats chain restarts. Every kChainLength steps the
    /// choice is taken without looking at the previous step, which bounds the cost of a
    /// stateless lookup; a repeat can only slip through at those points.
    static constexpr int kChainLength = 64;

private:
    static int chooseRandom (const ChooserConfig&, int64_t stepIndex) noexcept;
};

} // namespace mosaic::engine
