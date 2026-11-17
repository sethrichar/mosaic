#pragma once

// SlotSource implementations for engine tests. Every one is position-pure: the sample
// at a given source position never depends on what was read before.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "engine/SlotSource.h"

namespace mosaic::test
{

/// Left = the source position as a float (exact below 2^24), right = a constant slot tag.
/// Lets a test decode exactly which slot and which position produced each output sample.
class RampSource final : public engine::SlotSource
{
public:
    RampSource (int64_t length, float tag) : len (length), slotTag (tag) {}

    bool    isEmpty() const noexcept override         { return len <= 0; }
    bool    isLive() const noexcept override          { return false; }
    int64_t lengthInSamples() const noexcept override { return len; }

    void read (int64_t pos, float* l, float* r, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const int64_t p = pos + i;
            const bool in = p >= 0 && p < len;
            l[i] = in ? (float) p : 0.0f;
            r[i] = in ? slotTag : 0.0f;
        }
    }

    int64_t len;
    float slotTag;
};

/// Sine at a fixed frequency, phase a pure function of position. For click tests.
class SineSource final : public engine::SlotSource
{
public:
    SineSource (double freqHz, double sampleRate, float amp = 0.5f, int64_t length = 1 << 30)
        : freq (freqHz), rate (sampleRate), amplitude (amp), len (length) {}

    bool    isEmpty() const noexcept override         { return false; }
    bool    isLive() const noexcept override          { return false; }
    int64_t lengthInSamples() const noexcept override { return len; }

    void read (int64_t pos, float* l, float* r, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const double ph = 2.0 * M_PI * freq * (double) (pos + i) / rate;
            l[i] = r[i] = amplitude * (float) std::sin (ph);
        }
    }

    double freq, rate; float amplitude; int64_t len;
};

/// A live source: holds the audio of the current block only, addressed by timeline.
class LiveTestSource final : public engine::SlotSource
{
public:
    void setBlock (int64_t timelineStart, const float* l, const float* r, int n)
    {
        start = timelineStart;
        bufL.assign (l, l + n);
        bufR.assign (r, r + n);
    }

    bool    isEmpty() const noexcept override         { return false; }
    bool    isLive() const noexcept override          { return true; }
    int64_t lengthInSamples() const noexcept override { return 0; }

    void read (int64_t pos, float* l, float* r, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const int64_t idx = pos + i - start;
            const bool in = idx >= 0 && idx < (int64_t) bufL.size();
            l[i] = in ? bufL[(size_t) idx] : 0.0f;
            r[i] = in ? bufR[(size_t) idx] : 0.0f;
        }
    }

    int64_t start = 0;
    std::vector<float> bufL, bufR;
};

class EmptySource final : public engine::SlotSource
{
public:
    bool    isEmpty() const noexcept override         { return true; }
    bool    isLive() const noexcept override          { return false; }
    int64_t lengthInSamples() const noexcept override { return 0; }
    void    read (int64_t, float* l, float* r, int n) noexcept override
    {
        std::memset (l, 0, (size_t) n * sizeof (float));
        std::memset (r, 0, (size_t) n * sizeof (float));
    }
};

} // namespace mosaic::test
