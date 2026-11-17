#pragma once

// Background slot work (SPEC sections 6 and 7): decode, downmix/truncate to stereo,
// resample to the session rate, detect the key, render a pitch-shifted copy. Results
// are delivered on the message thread. Never touches the audio thread.

#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "analysis/KeyDetector.h"
#include "analysis/PitchShifter.h"
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
        std::unique_ptr<LoadedBuffer> buffer;               ///< loaded (or rendered) audio
        std::shared_ptr<const std::vector<float>> overview;
        double sourceSampleRate = 0.0;
        int    sourceChannels   = 0;

        bool   keyDetected = false;                         ///< detection ran
        analysis::KeyResult key;                            ///< valid when keyDetected

        bool   rendered = false;                            ///< buffer is a pitch-shifted render
        double renderedSemitones = 0.0;
    };

    /// Called on the message thread with the slot, the generation of the request and the result.
    using Callback = std::function<void (int slot, int generation, Result&&)>;

    FileLoader();
    ~FileLoader() override;

    void setCallback (Callback cb) { callback = std::move (cb); }

    /// Load (decode + resample), optionally detecting the key. A newer request for the
    /// same slot supersedes an older one and cancels it if already running.
    void requestLoad (int slot, int generation, const juce::File& file, double targetSampleRate, bool detectKey);

    /// Render a pitch-shifted copy. `original` may be null: the file is re-read from disk
    /// (SPEC 6: only the played buffer stays in RAM).
    void requestRender (int slot, int generation, const juce::File& file, double targetSampleRate,
                        std::unique_ptr<LoadedBuffer> original, double semitones);

    /// Jobs queued or running, for the offline-bounce wait (SPEC 7.6).
    int pendingJobs() const noexcept { return pending.load(); }

    /// 0..1 progress of the current render for a slot (UI).
    float renderProgress (int slot) const noexcept { return progress[slot].load(); }

    /// Synchronous load on the calling thread (tests, tools). Thread-safe.
    static Result loadNow (const juce::File& file, double targetSampleRate, bool detectKey = false,
                           std::atomic<bool>* cancel = nullptr);

    /// Synchronous render. Thread-safe.
    static Result renderNow (const LoadedBuffer& original, double semitones, std::atomic<bool>* cancel = nullptr,
                             std::atomic<float>* progressOut = nullptr);

    static std::shared_ptr<const std::vector<float>> makeOverview (const LoadedBuffer&, int bins = kOverviewBins);

private:
    void run() override;

    struct Job
    {
        enum class Kind { Load, Render } kind;
        int slot; int generation; juce::File file; double rate;
        bool detectKey;
        std::shared_ptr<LoadedBuffer> original;   // shared so the Job is copyable in the queue
        double semitones;
    };

    void post (int slot, int generation, std::shared_ptr<Result>);

    juce::CriticalSection lock;
    std::vector<Job> jobs;
    std::atomic<int> latestGeneration[engine::kNumSlots] {};
    std::atomic<bool> cancelFlags[engine::kNumSlots] {};
    std::atomic<float> progress[engine::kNumSlots] {};
    std::atomic<int> pending { 0 };
    juce::WaitableEvent wake;
    Callback callback;

    JUCE_DECLARE_WEAK_REFERENCEABLE (FileLoader)
};

} // namespace mosaic::sources
