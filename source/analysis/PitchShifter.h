#pragma once

// Offline pitch shifting behind a small interface (SPEC 7.6), so the library can be
// swapped later. Renders a whole buffer: same length as the input, latency compensated,
// deterministic. Never called on the audio thread.

#include <atomic>
#include <cstdint>

namespace mosaic::analysis
{

class PitchShifter
{
public:
    virtual ~PitchShifter() = default;

    /// Shifts stereo audio by `semitones` (fractional allowed). outL/outR hold exactly
    /// numSamples samples on return. Returns false if cancelled or on bad input.
    virtual bool shift (const float* inL, const float* inR, int64_t numSamples, double sampleRate,
                        double semitones, float* outL, float* outR, std::atomic<bool>* cancel = nullptr) = 0;

    virtual const char* name() const noexcept = 0;
};

/// Signalsmith Stretch (MIT), pinned in libs/signalsmith-stretch.
class SignalsmithPitchShifter final : public PitchShifter
{
public:
    bool shift (const float* inL, const float* inR, int64_t numSamples, double sampleRate,
                double semitones, float* outL, float* outR, std::atomic<bool>* cancel = nullptr) override;

    const char* name() const noexcept override { return "Signalsmith Stretch 1.4.0"; }

    /// Fixed seed: the library only randomises for large time-stretch ratios, but a
    /// fixed seed removes any doubt (SPEC 7.6: deterministic renders).
    static constexpr long kSeed = 20261116;
};

} // namespace mosaic::analysis
