#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "engine/GridClock.h"

using namespace mosaic::engine;

namespace
{
    // A synthetic host: ppq is derived exactly from the sample position.
    HostBlock hostAt (int64_t t, double bpm, double sr)
    {
        HostBlock b;
        b.bpm = bpm; b.sampleRate = sr; b.timeInSamples = t;
        b.ppqAtStart = (double) t / GridClock::samplesPerQuarter (b);
        b.playing = true;
        return b;
    }

    // Oracle: boundary k in absolute samples, computed once from the origin.
    int64_t oracleBoundary (double stepQN, double bpm, double sr, int64_t k)
    {
        return (int64_t) std::ceil ((double) k * stepQN * sr * 60.0 / bpm - GridClock::kSnapEpsilonSamples);
    }
}

TEST_CASE ("rate lengths in quarter notes", "[grid]")
{
    REQUIRE (Rate { RateBase::Quarter, RateMod::Straight }.lengthInQuarterNotes() == 1.0);
    REQUIRE (Rate { RateBase::Whole,   RateMod::Straight }.lengthInQuarterNotes() == 4.0);
    REQUIRE (Rate { RateBase::Eighth,  RateMod::Dotted   }.lengthInQuarterNotes() == 0.75);
    REQUIRE (Rate { RateBase::Eighth,  RateMod::Triplet  }.lengthInQuarterNotes() == 0.5 * 2.0 / 3.0);
    REQUIRE (Rate { RateBase::ThirtySecond, RateMod::Straight }.lengthInQuarterNotes() == 0.125);
    for (int i = 0; i < Rate::kNumChoices; ++i)
        REQUIRE (Rate::fromIndex (i).toIndex() == i);
}

TEST_CASE ("step boundaries are sample-exact for every rate, rate/tempo and block size", "[grid]")
{
    const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
    const double tempos[]      = { 120.0, 93.7, 174.0, 61.3 };
    const int    blockSizes[]  = { 1, 32, 512, 4096 };

    for (double sr : sampleRates)
        for (double bpm : tempos)
            for (int ri = 0; ri < Rate::kNumChoices; ++ri)
            {
                const double stepQN = Rate::fromIndex (ri).lengthInQuarterNotes();
                const int64_t spanSamples = (int64_t) (sr * 4.0);   // 4 seconds

                for (int n : blockSizes)
                {
                    // Walk the timeline in blocks of n; collect every boundary found.
                    std::vector<int64_t> found;
                    for (int64_t t = 0; t < spanSamples; t += n)
                    {
                        const HostBlock b = hostAt (t, bpm, sr);
                        int64_t k = GridClock::stepAtStart (b, stepQN);
                        REQUIRE (GridClock::boundaryOffset (b, stepQN, k) <= 0);
                        REQUIRE (GridClock::boundaryOffset (b, stepQN, k + 1) > 0);
                        if (GridClock::boundaryOffset (b, stepQN, k) == 0)
                            found.push_back (t);
                        for (int64_t j = k + 1;; ++j)
                        {
                            const int64_t off = GridClock::boundaryOffset (b, stepQN, j);
                            if (off >= n) break;
                            if (t + off < spanSamples)      // the last block may overrun the span
                                found.push_back (t + off);
                        }
                    }

                    // Compare against the oracle.
                    std::vector<int64_t> expected;
                    for (int64_t k = 0;; ++k)
                    {
                        const int64_t s = oracleBoundary (stepQN, bpm, sr, k);
                        if (s >= spanSamples) break;
                        expected.push_back (s);
                    }
                    if (found != expected)
                    {
                        std::string f, e;
                        for (auto v : found)    f += std::to_string (v) + " ";
                        for (auto v : expected) e += std::to_string (v) + " ";
                        FAIL ("mismatch sr=" << sr << " bpm=" << bpm << " rate=" << ri << " block=" << n
                              << "\n found:    " << f << "\n expected: " << e);
                    }
                }
            }
}

TEST_CASE ("negative (pre-roll) positions give negative steps and consistent boundaries", "[grid]")
{
    const HostBlock b = hostAt (-1000, 120.0, 48000.0);
    const double stepQN = 0.5;   // 1/8 at 120 = 12000 samples
    const int64_t k = GridClock::stepAtStart (b, stepQN);
    REQUIRE (k == -1);
    REQUIRE (GridClock::boundaryOffset (b, stepQN, 0) == 1000);
    REQUIRE (GridClock::boundaryOffset (b, stepQN, -1) == -11000);
    REQUIRE (GridClock::stepLengthSamples (b, stepQN, -1) == 12000);
}

TEST_CASE ("advancing a block keeps boundaries identical", "[grid]")
{
    const HostBlock a = hostAt (123456, 93.7, 44100.0);
    const HostBlock b = GridClock::advanced (a, 777);
    const double stepQN = Rate { RateBase::Sixteenth, RateMod::Triplet }.lengthInQuarterNotes();
    for (int64_t k = -3; k < 400; ++k)
        REQUIRE (GridClock::boundaryTimeline (a, stepQN, k) == GridClock::boundaryTimeline (b, stepQN, k));
}

TEST_CASE ("bars convert to steps and samples using the host time signature", "[grid]")
{
    REQUIRE (GridClock::quarterNotesPerBar (4, 4) == 4.0);
    REQUIRE (GridClock::quarterNotesPerBar (6, 8) == 3.0);
    REQUIRE (GridClock::quarterNotesPerBar (7, 8) == 3.5);
    REQUIRE (GridClock::quarterNotesPerBar (0, 0) == 4.0);

    REQUIRE (GridClock::patternLengthSteps (0, 4.0, 0.25) == 0);           // off
    REQUIRE (GridClock::patternLengthSteps (1, 4.0, 0.25) == 16);          // 1 bar of 1/16
    REQUIRE (GridClock::patternLengthSteps (2, 3.0, 0.5) == 12);           // 2 bars of 6/8 in 1/8
    REQUIRE (GridClock::patternLengthSteps (1, 4.0, 0.75) == 5);           // dotted 1/8: 5.33 -> 5
    REQUIRE (GridClock::patternLengthSteps (1, 4.0, 4.0) == 1);

    const double spq = 48000.0 * 60.0 / 120.0;                             // 24000
    REQUIRE (GridClock::barsToSamples (1.0, 4.0, spq) == 96000);
    REQUIRE (GridClock::barsToSamples (-0.5, 4.0, spq) == -48000);
    REQUIRE (GridClock::barsToSamples (0.25, 3.0, spq) == 18000);
}
