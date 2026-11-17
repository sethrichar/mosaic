#pragma once

// The Standalone build has no host transport (SPEC 4.1): this one gives it play/stop
// and a BPM. Driven by the UI through atomics, advanced by the audio thread.

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace mosaic
{

class StandaloneTransport final : public juce::AudioPlayHead
{
public:
    std::atomic<bool>    playing { false };
    std::atomic<double>  bpm { 120.0 };
    std::atomic<int64_t> positionSamples { 0 };
    std::atomic<double>  sampleRate { 48000.0 };

    juce::Optional<PositionInfo> getPosition() const override
    {
        const double sr = sampleRate.load();
        const double b  = bpm.load();
        const int64_t t = positionSamples.load();
        PositionInfo info;
        info.setIsPlaying (playing.load());
        info.setBpm (b);
        info.setTimeInSamples (t);
        info.setPpqPosition ((double) t / (sr * 60.0 / b));
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setPpqPositionOfLastBarStart (std::floor ((double) t / (sr * 60.0 / b) / 4.0) * 4.0);
        return info;
    }

    /// Audio thread, after each block.
    void advance (int n) noexcept
    {
        if (playing.load (std::memory_order_relaxed))
            positionSamples.fetch_add (n, std::memory_order_relaxed);
    }

    void rewind() noexcept { positionSamples.store (0); }
};

} // namespace mosaic
