#pragma once

#include <atomic>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/TileRenderer.h"
#include "hub/SendHub.h"
#include "plugin/Parameters.h"
#include "plugin/SlotInfo.h"
#include "plugin/StandaloneTransport.h"
#include "sources/FileLoader.h"
#include "sources/FileSlotSource.h"

namespace mosaic
{

/// Mosaic processor (M3: file mosaic, random mode; M1 probe and Send role retained).
///  - Mosaic role: the engine renders one tile at a time from the 8 file slots, driven by
///    the host playhead (or the Standalone transport). If a probe source is selected the
///    engine is bypassed and that source goes straight to the output (M1).
///  - Send role: writes the main input into the process-wide SendHub.
class MosaicAudioProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
{
public:
    static constexpr int kNumInputBuses = 8;
    static constexpr int kNumSlots = engine::kNumSlots;

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
    /// 1 = M0/M1 (no files), 2 = M3 (mosaic parameters + file paths).
    static constexpr int kStateVersion = 2;

    //==========================================================================
    /// Written on the audio thread, read by the UI and tests. Atomics only.
    struct Telemetry
    {
        // M1 probe
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
        std::atomic<bool>     skewValid { false };
        std::atomic<int64_t>  skewLast { 0 }, skewMin { 0 }, skewMax { 0 };
        std::atomic<int64_t>  neededLookBehind { 0 };
        std::atomic<uint64_t> sendReads { 0 }, sendUnderruns { 0 };
        std::atomic<bool>     resetStatsRequest { false };
        std::atomic<int>      desiredLatency { 0 };
        std::atomic<uint64_t> collisionCount { 0 };

        // M3 engine
        std::atomic<bool>     engineActive { false };       ///< engine rendered this block (not probe/Send)
        std::atomic<int>      activeSlot { -1 };            ///< slot playing at block start, -1 none
        std::atomic<int64_t>  stepIndex { 0 };
        std::atomic<int64_t>  slotPlayPos[kNumSlots] {};    ///< source position of the playing slot, -1 otherwise
        std::atomic<float>    outputPeak { 0.0f };
    };

    const Telemetry& telemetry() const noexcept { return tele; }
    Telemetry& telemetry() noexcept { return tele; }

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts; }
    int instanceId() const noexcept { return myInstanceId; }

    //==========================================================================
    // Files (message thread)

    void loadFile (int slot, const juce::File&);
    void clearSlot (int slot);
    const SlotInfo& slotInfo (int slot) const noexcept { return slots[slot]; }

    /// Synchronous load on the calling thread (tests / tools). Returns false on failure.
    bool loadFileNow (int slot, const juce::File&);

    size_t totalFileBytes() const noexcept;
    static constexpr size_t kMemoryWarningBytes = 1024ull * 1024 * 1024;

    /// New random seed (message thread).
    void reroll();

    //==========================================================================
    // Standalone transport (SPEC 4.1)
    StandaloneTransport& standaloneTransport() noexcept { return standalone; }
    bool usesStandaloneTransport() const noexcept { return wrapperType == wrapperType_Standalone; }

    /// Message-thread housekeeping (hub ownership, latency, buffer garbage, reloads).
    /// Normally driven by the timer; public so tests without a message loop can call it.
    void applyMessageThreadUpdates();

private:
    static BusesProperties makeBuses();
    void timerCallback() override { applyMessageThreadUpdates(); }

    void processSendRole (juce::AudioBuffer<float>&, int n, bool playing, bool haveTime, int64_t t);
    void processProbe (juce::AudioBuffer<float>&, int n, bool playing, bool haveTime, int64_t t, int src);
    void processEngine (juce::AudioBuffer<float>&, int n, const juce::Optional<juce::AudioPlayHead::PositionInfo>&);
    void copyInputBusToOutput (juce::AudioBuffer<float>&, int bus, int n);

    void applyLoadResult (int slot, sources::FileLoader::Result&&);
    juce::ValueTree filesToTree() const;
    void filesFromTree (const juce::ValueTree&);

    juce::AudioProcessorValueTreeState apvts;

    // Raw parameter pointers (atomics owned by the APVTS)
    std::atomic<float>* pRole;  std::atomic<float>* pGroup; std::atomic<float>* pSend; std::atomic<float>* pMute;
    std::atomic<float>* pProbeSource; std::atomic<float>* pLookBehind;
    std::atomic<float>* pRate; std::atomic<float>* pMode; std::atomic<float>* pPosition; std::atomic<float>* pFade;
    std::atomic<float>* pTileLength; std::atomic<float>* pSeed; std::atomic<float>* pPatternLength;
    std::atomic<float>* pAvoidRepeats; std::atomic<float>* pOutputGain;
    std::atomic<float>* pSlotGain[kNumSlots]; std::atomic<float>* pSlotMute[kNumSlots]; std::atomic<float>* pSlotWeight[kNumSlots];
    std::atomic<float>* pSlotStartOffset[kNumSlots]; std::atomic<float>* pSlotLoop[kNumSlots];

    engine::TileRenderer renderer;
    sources::FileSlotSource fileSources[kNumSlots];
    engine::SlotSource* slotPtrs[kNumSlots] {};
    engine::ChooserConfig chooserScratch;

    sources::FileLoader loader;
    SlotInfo slots[kNumSlots];
    double preparedSampleRate = 0.0;        // last rate given to prepareToPlay
    double loadedForSampleRate = 0.0;       // rate the current loads were requested at
    std::atomic<bool> reloadRequested { false };

    StandaloneTransport standalone;
    Telemetry tele;
    const int myInstanceId;
    int claimedChannel = -1;

    JUCE_DECLARE_WEAK_REFERENCEABLE (MosaicAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MosaicAudioProcessor)
};

} // namespace mosaic
