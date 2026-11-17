#pragma once

#include <atomic>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>

#include "hub/SendHub.h"
#include "plugin/Parameters.h"

namespace mosaic
{

/// M1 routing-spike processor. Two roles:
///  - Mosaic: 8 stereo input buses (1 main + 7 optional), a probe that routes exactly one
///    source (Input N or Send N) to the output, and telemetry for the probe UI.
///  - Send: writes its main input into the process-wide SendHub, passes audio through
///    (or mutes it).
class MosaicAudioProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
{
public:
    static constexpr int kNumInputBuses = 8;

    MosaicAudioProcessor();
    ~MosaicAudioProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return JucePlugin_WantsMidiInput != 0; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /// Bumped whenever the saved state layout changes (CLAUDE.md rule 7).
    static constexpr int kStateVersion = 1;

    //==========================================================================
    /// Written on the audio thread, read by the probe UI and tests. Atomics only.
    struct Telemetry
    {
        std::atomic<float>    inputPeak[kNumInputBuses] {};
        std::atomic<bool>     inputEnabled[kNumInputBuses] {};
        std::atomic<float>    sendPeak[hub::kNumSends] {};
        std::atomic<float>    sendRolePeak { 0.0f };

        std::atomic<bool>     hasPlayhead { false }, isPlaying { false }, isOffline { false };
        std::atomic<bool>     hasTimeInSamples { false }, hasPpq { false }, hasBpm { false };
        std::atomic<double>   ppq { 0.0 }, bpm { 0.0 }, sampleRate { 0.0 };
        std::atomic<int64_t>  timeInSamples { 0 };
        std::atomic<int>      blockSize { 0 }, maxBlockSize { 0 }, preparedBlockSize { 0 };
        std::atomic<uint64_t> blockCounter { 0 };

        // Send skew stats (Mosaic role, probe source = Send): head - T per block.
        std::atomic<bool>     skewValid { false };
        std::atomic<int64_t>  skewLast { 0 }, skewMin { 0 }, skewMax { 0 };
        /// Largest (blockSize - skew) seen: the smallest look-behind that would have had
        /// zero underruns over the run. This is the number M1 is after.
        std::atomic<int64_t>  neededLookBehind { 0 };
        std::atomic<uint64_t> sendReads { 0 }, sendUnderruns { 0 };
        std::atomic<bool>     resetStatsRequest { false };

        std::atomic<int>      desiredLatency { 0 };
        std::atomic<uint64_t> collisionCount { 0 };
    };

    const Telemetry& telemetry() const noexcept { return tele; }
    Telemetry& telemetry() noexcept { return tele; }

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts; }
    int instanceId() const noexcept { return myInstanceId; }

    /// Message-thread housekeeping (ring allocation, ownership, latency). Normally driven
    /// by the timer; public so tests without a message loop can call it directly.
    void applyMessageThreadUpdates();

private:
    static BusesProperties makeBuses();
    void timerCallback() override { applyMessageThreadUpdates(); }

    void processSendRole (juce::AudioBuffer<float>&, int n, bool playing, bool haveTime, int64_t t);
    void processMosaicRole (juce::AudioBuffer<float>&, int n, bool playing, bool haveTime, int64_t t);
    void copyInputBusToOutput (juce::AudioBuffer<float>&, int bus, int n);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* pRole;
    std::atomic<float>* pGroup;
    std::atomic<float>* pSend;
    std::atomic<float>* pMute;
    std::atomic<float>* pProbeSource;
    std::atomic<float>* pLookBehind;

    Telemetry tele;
    const int myInstanceId;
    int claimedChannel = -1;   // hub channel index this instance owns as a Send, or -1

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MosaicAudioProcessor)
};

} // namespace mosaic
