// Renders a short test-tone mosaic with the headless engine to a WAV file, so the M2
// engine can be heard without a host. Plain C++, no JUCE.
//
//   mosaic_engine_demo [out.wav] [seconds] [rateIndex 0-17] [fadeMs] [free 0/1]

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "engine/TileRenderer.h"

using namespace mosaic::engine;

namespace
{
    // A sine with a soft 8th-note pulse so tiles are easy to tell apart by ear.
    class ToneSource final : public SlotSource
    {
    public:
        ToneSource (double hz, double sr) : freq (hz), rate (sr) {}
        bool isEmpty() const noexcept override { return false; }
        bool isLive() const noexcept override { return false; }
        int64_t lengthInSamples() const noexcept override { return (int64_t) rate * 8; }
        void read (int64_t pos, float* l, float* r, int n) noexcept override
        {
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) (pos + i) / rate;
                const double pulse = 0.6 + 0.4 * std::fabs (std::sin (2.0 * M_PI * 2.0 * t));
                const float v = (float) (0.4 * pulse * std::sin (2.0 * M_PI * freq * t));
                l[i] = v; r[i] = v;
            }
        }
        double freq, rate;
    };

    void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sampleRate)
    {
        const uint32_t frames = (uint32_t) L.size();
        const uint32_t dataBytes = frames * 2 * 2;
        std::ofstream f (path, std::ios::binary);
        auto u32 = [&f] (uint32_t v) { f.write ((const char*) &v, 4); };
        auto u16 = [&f] (uint16_t v) { f.write ((const char*) &v, 2); };
        f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
        f.write ("fmt ", 4); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sampleRate); u32 ((uint32_t) sampleRate * 4); u16 (4); u16 (16);
        f.write ("data", 4); u32 (dataBytes);
        for (uint32_t i = 0; i < frames; ++i)
        {
            const auto clip = [] (float v) { return (int16_t) std::lround (std::fmax (-1.0f, std::fmin (1.0f, v)) * 32767.0f); };
            u16 ((uint16_t) clip (L[i])); u16 ((uint16_t) clip (R[i]));
        }
    }
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "engine-demo.wav";
    const double seconds  = argc > 2 ? std::atof (argv[2]) : 16.0;
    const int rateIndex   = argc > 3 ? std::atoi (argv[3]) : Rate { RateBase::Sixteenth, RateMod::Straight }.toIndex();
    const double fadeMs   = argc > 4 ? std::atof (argv[4]) : 5.0;
    const bool freeMode   = argc > 5 && std::atoi (argv[5]) != 0;

    const double sr = 48000.0, bpm = 120.0;
    std::vector<std::unique_ptr<SlotSource>> keep;
    SlotSource* slots[kNumSlots] = {};
    RenderParams p;
    ChooserConfig c;
    p.rate = Rate::fromIndex (rateIndex);
    p.fadeMs = fadeMs;
    p.freePosition = freeMode;
    c.seed = p.seed = 2026;
    c.avoidRepeats = true;

    const double notes[kNumSlots] = { 220.0, 261.63, 329.63, 392.0, 440.0, 523.25, 659.25, 783.99 };
    for (int s = 0; s < kNumSlots; ++s)
    {
        keep.push_back (std::make_unique<ToneSource> (notes[s], sr));
        slots[s] = keep.back().get();
        c.available[s] = true;
    }

    TileRenderer renderer;
    renderer.prepare (512);
    const int64_t total = (int64_t) (seconds * sr);
    std::vector<float> L ((size_t) total), R ((size_t) total);

    for (int64_t t = 0; t < total; t += 512)
    {
        HostBlock h;
        h.sampleRate = sr; h.bpm = bpm; h.timeInSamples = t; h.playing = true;
        h.ppqAtStart = (double) t / (sr * 60.0 / bpm);
        const int n = (int) std::min<int64_t> (512, total - t);
        renderer.render (h, p, c, slots, L.data() + t, R.data() + t, n);
    }

    writeWav (out, L, R, (int) sr);
    std::printf ("wrote %s: %.1f s, rate %s%s, fade %.1f ms, %s\n", out.c_str(), seconds,
                 Rate::baseName (p.rate.base), Rate::modSuffix (p.rate.mod), fadeMs, freeMode ? "Free" : "Locked");
    return 0;
}
