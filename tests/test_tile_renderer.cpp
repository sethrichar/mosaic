#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "engine/TileRenderer.h"
#include "TestSources.h"

using namespace mosaic::engine;
using namespace mosaic::test;

namespace
{
    struct Harness
    {
        double sr = 48000.0, bpm = 120.0;
        TileRenderer renderer;
        RenderParams p;
        ChooserConfig c;
        SlotSource* slots[kNumSlots] = {};
        std::vector<float> L, R;

        explicit Harness (int prepared = 4096)
        {
            renderer.prepare (prepared);
            p.rate = { RateBase::Sixteenth, RateMod::Straight };   // 6000 samples at 120/48k
            p.fadeMs = 0.0;
            c.seed = p.seed = 11;
        }

        double spq() const { return sr * 60.0 / bpm; }
        double stepQN() const { return p.rate.lengthInQuarterNotes(); }

        HostBlock host (int64_t t, bool playing = true) const
        {
            HostBlock b;
            b.sampleRate = sr; b.bpm = bpm; b.timeInSamples = t; b.playing = playing;
            b.ppqAtStart = (double) t / spq();
            return b;
        }

        void render (int64_t start, int64_t end, const std::vector<int>& pattern)
        {
            L.assign ((size_t) (end - start), 0.0f);
            R.assign ((size_t) (end - start), 0.0f);
            int64_t t = start; size_t pi = 0;
            while (t < end)
            {
                const int n = (int) std::min<int64_t> (pattern[pi++ % pattern.size()], end - t);
                renderer.render (host (t), p, c, slots, L.data() + (t - start), R.data() + (t - start), n);
                t += n;
            }
        }

        int64_t stepOf (int64_t t) const { return GridClock::stepAtStart (host (t), stepQN()); }
        int64_t stepStart (int64_t t) const { return GridClock::boundaryTimeline (host (t), stepQN(), stepOf (t)); }
        int64_t stepLen (int64_t t) const { return GridClock::stepLengthSamples (host (t), stepQN(), stepOf (t)); }
    };

    float maxAbsJump (const std::vector<float>& v)
    {
        float m = 0.0f;
        for (size_t i = 1; i < v.size(); ++i) m = std::max (m, std::fabs (v[i] - v[i - 1]));
        return m;
    }
}

TEST_CASE ("all-empty slots or stopped transport give silence without crashing", "[renderer]")
{
    Harness h;
    EmptySource empty;
    for (auto& s : h.slots) s = &empty;
    h.render (0, 20000, { 512 });
    for (float v : h.L) REQUIRE (v == 0.0f);

    RampSource ramp (48000, 0.5f);
    h.slots[0] = &ramp; h.c.available[0] = true;
    h.L.assign (1024, 1.0f); h.R.assign (1024, 1.0f);
    h.renderer.render (h.host (5000, false), h.p, h.c, h.slots, h.L.data(), h.R.data(), 1024);
    for (float v : h.L) REQUIRE (v == 0.0f);
    h.renderer.render (h.host (5000), h.p, h.c, h.slots, h.L.data(), h.R.data(), 0);   // zero-length block
}

TEST_CASE ("locked mode plays the chosen slot in sync with the timeline", "[renderer]")
{
    Harness h;
    RampSource a (10000, 0.1f), b (7000, 0.2f), cfile (48000, 0.3f);
    h.slots[0] = &a; h.slots[1] = &b; h.slots[2] = &cfile;
    h.c.available[0] = h.c.available[1] = h.c.available[2] = true;
    h.p.placement[1].startOffsetSamples = 2500;
    h.p.placement[2].loop = false;
    h.p.slotGain[2] = 0.5f;

    const int64_t start = 3000, end = 120000;
    h.render (start, end, { 512 });

    int seen[3] = { 0, 0, 0 };
    for (int64_t t = start; t < end; ++t)
    {
        const int slot = StepChooser::choose (h.c, h.stepOf (t));
        const float tag = h.R[(size_t) (t - start)];
        const float val = h.L[(size_t) (t - start)];
        ++seen[slot];
        if (slot == 0)      { REQUIRE (tag == 0.1f); REQUIRE (val == (float) (t % 10000)); }
        else if (slot == 1) { REQUIRE (tag == 0.2f); REQUIRE (val == (float) (((t - 2500) % 7000 + 7000) % 7000)); }
        else                { REQUIRE (tag == (t < 48000 ? 0.15f : 0.0f)); REQUIRE (val == (t < 48000 ? 0.5f * (float) t : 0.0f)); }
    }
    REQUIRE (seen[0] > 0); REQUIRE (seen[1] > 0); REQUIRE (seen[2] > 0);
}

TEST_CASE ("free mode reads a hashed slice per tile, contiguously", "[renderer]")
{
    Harness h;
    RampSource a (60000, 0.1f);
    h.slots[0] = &a; h.c.available[0] = true;
    h.p.freePosition = true;

    const int64_t start = 0, end = 90000;
    h.render (start, end, { 256 });
    for (int64_t t = start; t < end; ++t)
    {
        const int64_t k = h.stepOf (t), S = h.stepStart (t), len = h.stepLen (t);
        const int64_t slice = SlicePosition::freeSliceIndex (60000, len, k, 0, h.p.seed);
        REQUIRE (h.L[(size_t) t] == (float) (slice * len + (t - S)));
    }
}

TEST_CASE ("rendering bars 5-8 alone equals bars 1-8 in one pass", "[renderer][determinism]")
{
    const int64_t barSamples = 4 * 24000;   // 4/4 at 120 BPM, 48 kHz
    auto make = [] (Harness& h, std::vector<std::unique_ptr<SlotSource>>& keep) {
        h.p.fadeMs = 5.0;
        for (int s = 0; s < 5; ++s)
        {
            keep.push_back (std::make_unique<SineSource> (220.0 * (s + 1), h.sr));
            h.slots[s] = keep.back().get();
            h.c.available[s] = true;
        }
        h.c.avoidRepeats = true;
    };
    std::vector<std::unique_ptr<SlotSource>> keepA, keepB;
    Harness full, partial;
    make (full, keepA); make (partial, keepB);

    full.render (0, 8 * barSamples, { 512 });
    partial.render (4 * barSamples, 8 * barSamples, { 512 });

    for (int64_t t = 4 * barSamples; t < 8 * barSamples; ++t)
    {
        REQUIRE (full.L[(size_t) t] == partial.L[(size_t) (t - 4 * barSamples)]);
        REQUIRE (full.R[(size_t) t] == partial.R[(size_t) (t - 4 * barSamples)]);
    }
}

TEST_CASE ("variable block sizes and oversize blocks render bit-identically", "[renderer][determinism]")
{
    std::vector<std::unique_ptr<SlotSource>> keep;
    auto make = [&] (Harness& h) {
        h.p.fadeMs = 5.0; h.p.freePosition = true; h.p.tileLengthPercent = 80.0;
        for (int s = 0; s < 4; ++s)
        {
            keep.push_back (std::make_unique<SineSource> (300.0 + 70.0 * s, h.sr, 0.5f, 30000));
            h.slots[s] = keep.back().get();
            h.c.available[s] = true;
        }
    };
    Harness big (8192), varied (8192), small (256);
    make (big); make (varied); make (small);

    const int64_t end = 8192 * 6;
    big.render (0, end, { 8192 });
    varied.render (0, end, { 1, 7, 64, 512, 3, 4096, 100, 33 });
    small.render (0, end, { 8192 });   // prepared for 256: the renderer must chunk internally

    REQUIRE (big.L == varied.L);
    REQUIRE (big.R == varied.R);
    REQUIRE (big.L == small.L);
    REQUIRE (big.R == small.R);
}

TEST_CASE ("tile edges never jump more than the fade allows", "[renderer][click]")
{
    std::vector<std::unique_ptr<SlotSource>> keep;
    Harness h;
    const double maxFreq = 1000.0;
    const float amp = 0.5f;
    for (int s = 0; s < 6; ++s)
    {
        keep.push_back (std::make_unique<SineSource> (200.0 + 160.0 * s, h.sr, amp));
        h.slots[s] = keep.back().get();
        h.c.available[s] = true;
    }
    h.p.rate = { RateBase::ThirtySecond, RateMod::Triplet };   // short tiles: many edges
    h.p.fadeMs = 5.0;
    const double fadeSamples = h.p.fadeMs * h.sr / 1000.0;
    // A sine moves at most 2*pi*f*A/sr per sample; the crossfade adds at most 2A/fade.
    const float bound = (float) (2.0 * M_PI * maxFreq * amp / h.sr + 2.0 * amp / fadeSamples) * 1.05f;

    h.render (0, 48000 * 4, { 480 });
    REQUIRE (maxAbsJump (h.L) <= bound);

    SECTION ("with fade 0 there are real discontinuities (sanity check of the test)")
    {
        h.p.fadeMs = 0.0;
        h.render (0, 48000 * 4, { 480 });
        REQUIRE (maxAbsJump (h.L) > bound);
    }
    SECTION ("gated tiles also respect the bound")
    {
        h.p.tileLengthPercent = 50.0;
        h.render (0, 48000 * 4, { 480 });
        REQUIRE (maxAbsJump (h.L) <= bound);
    }
}

TEST_CASE ("tile length below 100% leaves silence before the next tile", "[renderer]")
{
    Harness h;
    RampSource a (1 << 23, 0.1f);
    h.slots[0] = &a; h.c.available[0] = true;
    h.p.tileLengthPercent = 50.0;
    h.p.fadeMs = 0.0;

    h.render (0, 60000, { 1000 });
    for (int64_t t = 0; t < 60000; ++t)
    {
        const int64_t S = h.stepStart (t), len = h.stepLen (t);
        const bool audible = (t - S) < len / 2;
        if (audible) REQUIRE (h.R[(size_t) t] == 0.1f);
        else         REQUIRE (h.R[(size_t) t] == 0.0f);
    }
}

TEST_CASE ("a live slot passes through seamlessly across tile boundaries", "[renderer][live]")
{
    Harness h;
    LiveTestSource live;
    h.slots[3] = &live; h.c.available[3] = true;
    h.p.fadeMs = 5.0;

    const int n = 512;
    std::vector<float> inL (n), inR (n), outL (n), outR (n);
    for (int64_t t = 0; t < 48000; t += n)
    {
        for (int i = 0; i < n; ++i) { inL[(size_t) i] = (float) ((t + i) % 977) * 0.001f; inR[(size_t) i] = -inL[(size_t) i]; }
        live.setBlock (t, inL.data(), inR.data(), n);
        h.renderer.render (h.host (t), h.p, h.c, h.slots, outL.data(), outR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            REQUIRE (outL[(size_t) i] == inL[(size_t) i]);   // same slot every step: seamless, no fade applied
            REQUIRE (outR[(size_t) i] == inR[(size_t) i]);
        }
    }
}

TEST_CASE ("sequence mode with rests renders silence on rest steps", "[renderer]")
{
    Harness h;
    RampSource a (1 << 23, 0.1f), b (1 << 23, 0.2f);
    h.slots[0] = &a; h.slots[1] = &b;
    h.c.available[0] = h.c.available[1] = true;
    h.c.mode = ChooseMode::Sequence;
    h.c.sequenceLength = 3;
    h.c.sequence[0] = 1; h.c.sequence[1] = ChooserConfig::kRest; h.c.sequence[2] = 0;

    h.render (0, 60000, { 777 });
    for (int64_t t = 0; t < 60000; ++t)
    {
        const int64_t k = h.stepOf (t);
        const float expect = (k % 3 == 0) ? 0.2f : (k % 3 == 1) ? 0.0f : 0.1f;
        REQUIRE (h.R[(size_t) t] == expect);
    }
}
