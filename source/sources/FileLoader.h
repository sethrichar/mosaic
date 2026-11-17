#pragma once

// Background file loading (SPEC section 6): decode, downmix/truncate to stereo, resample
// to the session rate with a windowed-sinc interpolator, build a waveform overview.
// Results are delivered on the message thread. Never touches the audio thread.

#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "engine/StepChooser.h"
#include "sources/LoadedBuffer.h"

namespace mosaic::sources
{

class FileLoader final : private juce::Thread
{
public:
    static constexpr int kOverviewBins = 1024;

    struct Result
    {
        bool ok = false;
        juce::String error;
        std::unique_ptr<LoadedBuffer> buffer;
        std::shared_ptr<const std::vector<float>> overview;
        double sourceSampleRate = 0.0;
        int    sourceChannels   = 0;
    };

    /// Called on the message thread with the slot, the generation of the request and the result.
    using Callback = std::function<void (int slot, int generation, Result&&)>;

    FileLoader();
    ~FileLoader() override;

    void setCallback (Callback cb) { callback = std::move (cb); }

    /// Queue a load. A newer request for the same slot supersedes an older one.
    void request (int slot, int generation, const juce::File& file, double targetSampleRate);

    /// Synchronous load on the calling thread (tests, offline use). Thread-safe.
    static Result loadNow (const juce::File& file, double targetSampleRate);

    static std::shared_ptr<const std::vector<float>> makeOverview (const LoadedBuffer&, int bins = kOverviewBins);

private:
    void run() override;

    struct Job { int slot; int generation; juce::File file; double rate; };

    juce::CriticalSection lock;
    std::vector<Job> jobs;
    std::atomic<int> latestGeneration[engine::kNumSlots] {};
    juce::WaitableEvent wake;
    Callback callback;

    JUCE_DECLARE_WEAK_REFERENCEABLE (FileLoader)
};

} // namespace mosaic::sources
