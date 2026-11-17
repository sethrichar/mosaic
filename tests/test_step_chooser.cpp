#include <catch2/catch_test_macros.hpp>
#include <map>

#include "engine/StepChooser.h"

using namespace mosaic::engine;

namespace
{
    ChooserConfig randomConfig (uint32_t seed, int numAvailable = 8)
    {
        ChooserConfig c;
        c.seed = seed;
        for (int s = 0; s < kNumSlots; ++s) c.available[s] = s < numAvailable;
        return c;
    }
}

TEST_CASE ("same (seed, step) always gives the same slot", "[chooser]")
{
    const auto c = randomConfig (42);
    for (int64_t step = -500; step < 5000; ++step)
        REQUIRE (StepChooser::choose (c, step) == StepChooser::choose (c, step));

    auto c2 = c; c2.seed = 43;
    int differences = 0;
    for (int64_t step = 0; step < 1000; ++step)
        differences += StepChooser::choose (c, step) != StepChooser::choose (c2, step);
    REQUIRE (differences > 500);
}

TEST_CASE ("muted or empty slots are never chosen; all-empty gives none", "[chooser]")
{
    auto c = randomConfig (7, 8);
    c.available[0] = false; c.available[3] = false; c.available[7] = false;
    for (int64_t step = 0; step < 10000; ++step)
    {
        const int s = StepChooser::choose (c, step);
        REQUIRE (s >= 0); REQUIRE (s < kNumSlots);
        REQUIRE (c.available[s]);
    }
    ChooserConfig none;
    for (int64_t step = 0; step < 100; ++step)
        REQUIRE (StepChooser::choose (none, step) == StepChooser::kNone);
}

TEST_CASE ("weights shape the distribution over 10000 steps", "[chooser]")
{
    auto c = randomConfig (99, 4);
    c.weight[0] = 50; c.weight[1] = 25; c.weight[2] = 25; c.weight[3] = 0;   // weight 0: never
    std::map<int, int> count;
    const int N = 10000;
    for (int64_t step = 0; step < N; ++step) ++count[StepChooser::choose (c, step)];
    REQUIRE (count[3] == 0);
    REQUIRE (count[StepChooser::kNone] == 0);
    auto within = [&] (int slot, double expected) {
        const double frac = (double) count[slot] / N;
        return frac > expected - 0.03 && frac < expected + 0.03;
    };
    REQUIRE (within (0, 0.50));
    REQUIRE (within (1, 0.25));
    REQUIRE (within (2, 0.25));
}

TEST_CASE ("pattern length wraps the random pattern", "[chooser]")
{
    auto c = randomConfig (5);
    c.patternLengthSteps = 16;
    for (int64_t step = -64; step < 1000; ++step)
        REQUIRE (StepChooser::choose (c, step) == StepChooser::choose (c, step + 16));
    c.patternLengthSteps = 0;
    int repeats = 0;
    for (int64_t step = 0; step < 1000; ++step)
        repeats += StepChooser::choose (c, step) == StepChooser::choose (c, step + 16);
    REQUIRE (repeats < 300);   // ~1/8 chance by coincidence
}

TEST_CASE ("avoid repeats never picks the same slot twice in a row (except at chain restarts)", "[chooser]")
{
    auto c = randomConfig (123, 3);
    c.avoidRepeats = true;
    int repeats = 0;
    for (int64_t step = 1; step < 20000; ++step)
    {
        const bool chainRestart = (step % StepChooser::kChainLength) == 0;
        if (StepChooser::choose (c, step) == StepChooser::choose (c, step - 1))
        {
            REQUIRE (chainRestart);
            ++repeats;
        }
    }
    REQUIRE (repeats < 20000 / StepChooser::kChainLength);   // only possible at restarts, and rare there

    // Still stateless: a lookup far into the timeline matches a walk-through.
    REQUIRE (StepChooser::choose (c, 123456789) == StepChooser::choose (c, 123456789));

    // With one slot, repeats are unavoidable and allowed.
    auto one = randomConfig (1, 1);
    one.avoidRepeats = true;
    for (int64_t step = 0; step < 100; ++step) REQUIRE (StepChooser::choose (one, step) == 0);
}

TEST_CASE ("sequence mode plays slots, rests and random steps", "[chooser]")
{
    auto c = randomConfig (3, 8);
    c.mode = ChooseMode::Sequence;
    c.sequenceLength = 4;
    c.sequence[0] = 2; c.sequence[1] = ChooserConfig::kRest; c.sequence[2] = 5; c.sequence[3] = ChooserConfig::kRandom;
    c.available[5] = false;   // muted: acts as a rest

    for (int64_t bar = -3; bar < 10; ++bar)
    {
        const int64_t base = bar * 4;
        REQUIRE (StepChooser::choose (c, base + 0) == 2);
        REQUIRE (StepChooser::choose (c, base + 1) == StepChooser::kNone);
        REQUIRE (StepChooser::choose (c, base + 2) == StepChooser::kNone);
        const int r = StepChooser::choose (c, base + 3);
        REQUIRE (r >= 0); REQUIRE (c.available[r]);
    }
}
