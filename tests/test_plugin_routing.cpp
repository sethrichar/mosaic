// In-process routing tests for the M1 probe: drives two real processors (one Send, one
// Mosaic) with a fake playhead and checks that the Send path nulls sample-exactly, that
// it is independent of processing order given enough look-behind, and that all 8 native
// input buses are readable.

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_utils/juce_audio_utils.h>

#include "plugin/PluginProcessor.h"

using mosaic::MosaicAudioProcessor;
namespace params = mosaic::params;

namespace
{
    struct FakePlayHead final : juce::AudioPlayHead
    {
        int64_t timeInSamples = 0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setTimeInSamples (timeInSamples);
            info.setIsPlaying (playing);
            info.setBpm (120.0);
            info.setPpqPosition ((double) timeInSamples / 48000.0 * 2.0);
            return info;
        }
    };

    void setChoice (MosaicAudioProcessor& p, const char* id, int index)
    {
        auto* param = p.state().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        p.applyMessageThreadUpdates();
    }

    int lookBehindIndexFor (int samples)
    {
        for (int i = 0; i < params::kNumLookBehindChoices; ++i)
            if (params::kLookBehindSamples[i] == samples) return i;
        FAIL ("no such look-behind choice");
        return 0;
    }

    float rampValue (int64_t pos) { return (float) (pos % 10007) * 1.0e-4f; }

    void fillRamp (juce::AudioBuffer<float>& b, int64_t pos, float scale = 1.0f)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (c, i, rampValue (pos + i) * scale * (c == 0 ? 1.0f : -1.0f));
    }

    // A pair of processors plus the scoped JUCE runtime they need.
    struct Rig
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        FakePlayHead playhead;
        MosaicAudioProcessor send, mosaic;
        juce::MidiBuffer midi;
        static constexpr int kBlock = 256;
        static constexpr double kRate = 48000.0;

        // The hub is process-wide: every rig takes a fresh Send channel in Group B so
        // data from an earlier test case can never satisfy a later read.
        static int nextSend()
        {
            static int counter = 0;
            return counter++ % 8;
        }

        Rig (int lookBehindSamples)
        {
            const int sendIndex = nextSend();
            for (auto* p : { &send, &mosaic })
            {
                p->setPlayHead (&playhead);
                p->setRateAndBufferSizeDetails (kRate, kBlock);
                p->prepareToPlay (kRate, kBlock);
            }
            setChoice (send, params::role, 1);
            setChoice (send, params::sendGroup, 1);          // Group B, to stay clear of other tests
            setChoice (send, params::sendNumber, sendIndex);
            setChoice (mosaic, params::role, 0);
            setChoice (mosaic, params::sendGroup, 1);
            setChoice (mosaic, params::probeSource, params::probeSourceSend + sendIndex);
            setChoice (mosaic, params::probeLookBehind, lookBehindIndexFor (lookBehindSamples));
        }

        ~Rig()
        {
            send.releaseResources();
            mosaic.releaseResources();
        }

        void runSend (int n = kBlock)
        {
            juce::AudioBuffer<float> b (2, n);
            fillRamp (b, playhead.timeInSamples);
            send.processBlock (b, midi);
        }

        juce::AudioBuffer<float> runMosaic (int n = kBlock)
        {
            juce::AudioBuffer<float> b (2, n);
            b.clear();
            mosaic.processBlock (b, midi);
            return b;
        }

        static void expectRamp (const juce::AudioBuffer<float>& out, int64_t pos)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < out.getNumSamples(); ++i)
                {
                    const float expected = rampValue (pos + i) * (c == 0 ? 1.0f : -1.0f);
                    if (out.getSample (c, i) != expected)
                        FAIL ("channel " << c << " sample " << i << ": got " << out.getSample (c, i)
                              << " expected " << expected << " at pos " << pos);
                }
        }

        static void expectSilence (const juce::AudioBuffer<float>& out)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    if (out.getSample (c, i) != 0.0f)
                        FAIL ("expected silence, channel " << c << " sample " << i);
        }
    };
}

TEST_CASE ("Send -> Mosaic nulls sample-exactly when the Send is processed first", "[routing]")
{
    const int L = 512;
    Rig rig (L);
    rig.mosaic.applyMessageThreadUpdates();
    REQUIRE (rig.mosaic.getLatencySamples() == 0);   // nothing processed yet

    for (int block = 0; block < 40; ++block)
    {
        rig.runSend();
        auto out = rig.runMosaic();
        const int64_t T = rig.playhead.timeInSamples;
        if (T - L >= 0)
            Rig::expectRamp (out, T - L);              // delayed by exactly L: host PDC undoes it
        else
            Rig::expectSilence (out);                  // warm-up: nothing written before T=0
        rig.playhead.timeInSamples += Rig::kBlock;
    }

    rig.mosaic.applyMessageThreadUpdates();
    REQUIRE (rig.mosaic.getLatencySamples() == L);
    REQUIRE (rig.mosaic.telemetry().sendUnderruns.load() == 2);   // the two warm-up blocks
    REQUIRE (rig.mosaic.telemetry().skewLast.load() == Rig::kBlock);
    REQUIRE (rig.mosaic.telemetry().neededLookBehind.load() == 0); // Send first: no look-behind needed
}

TEST_CASE ("Send -> Mosaic still nulls when the Mosaic is processed first (look-behind >= block)", "[routing]")
{
    const int L = 256;   // exactly one block
    Rig rig (L);

    for (int block = 0; block < 40; ++block)
    {
        auto out = rig.runMosaic();
        rig.runSend();
        const int64_t T = rig.playhead.timeInSamples;
        if (block == 0)
            Rig::expectSilence (out);
        else
            Rig::expectRamp (out, T - L);
        rig.playhead.timeInSamples += Rig::kBlock;
    }
    REQUIRE (rig.mosaic.telemetry().sendUnderruns.load() == 1);
    REQUIRE (rig.mosaic.telemetry().skewMin.load() == 0);   // producer exactly one block behind
    REQUIRE (rig.mosaic.telemetry().neededLookBehind.load() == Rig::kBlock);
}

TEST_CASE ("with zero look-behind, Mosaic-before-Send underruns to silence every block", "[routing]")
{
    Rig rig (0);
    for (int block = 0; block < 10; ++block)
    {
        auto out = rig.runMosaic();
        rig.runSend();
        Rig::expectSilence (out);
        rig.playhead.timeInSamples += Rig::kBlock;
    }
    REQUIRE (rig.mosaic.telemetry().sendUnderruns.load() == 10);
    REQUIRE (rig.mosaic.telemetry().skewMax.load() == 0);   // producer is exactly one block behind
    REQUIRE (rig.mosaic.telemetry().skewMin.load() == 0);
    REQUIRE (rig.mosaic.telemetry().neededLookBehind.load() == Rig::kBlock);   // what the readout would tell Seth
}

TEST_CASE ("variable block sizes keep the Send path aligned", "[routing]")
{
    const int L = 1024;
    Rig rig (L);
    const int sizes[] = { 256, 17, 1000, 1, 512, 64, 777, 256 };
    for (int pass = 0; pass < 20; ++pass)
        for (int n : sizes)
        {
            rig.runSend (n);
            auto out = rig.runMosaic (n);
            const int64_t T = rig.playhead.timeInSamples;
            if (T - L >= 0) Rig::expectRamp (out, T - L);
            rig.playhead.timeInSamples += n;
        }
}

TEST_CASE ("a stopped transport reads as silence", "[routing]")
{
    Rig rig (256);
    rig.playhead.playing = false;
    for (int block = 0; block < 5; ++block)
    {
        rig.runSend();
        Rig::expectSilence (rig.runMosaic());
        rig.playhead.timeInSamples += Rig::kBlock;
    }
}

TEST_CASE ("all 8 native input buses are readable through the probe", "[routing][buses]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MosaicAudioProcessor p;
    REQUIRE (p.getBusCount (true) == MosaicAudioProcessor::kNumInputBuses);
    REQUIRE (p.getBus (true, 0)->isEnabled());
    for (int b = 1; b < MosaicAudioProcessor::kNumInputBuses; ++b)
        REQUIRE_FALSE (p.getBus (true, b)->isEnabled());      // plain stereo by default

    REQUIRE (p.enableAllBuses());
    for (int b = 0; b < MosaicAudioProcessor::kNumInputBuses; ++b)
        REQUIRE (p.getBus (true, b)->isEnabled());

    const int n = 64;
    p.setRateAndBufferSizeDetails (48000.0, n);
    p.prepareToPlay (48000.0, n);
    REQUIRE (p.getTotalNumInputChannels() == 16);

    juce::MidiBuffer midi;
    for (int src = 0; src < MosaicAudioProcessor::kNumInputBuses; ++src)
    {
        setChoice (p, params::probeSource, params::probeSourceInput + src);

        juce::AudioBuffer<float> buf (juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), n);
        for (int b = 0; b < MosaicAudioProcessor::kNumInputBuses; ++b)
        {
            auto bb = p.getBusBuffer (buf, true, b);
            for (int c = 0; c < bb.getNumChannels(); ++c)
                for (int i = 0; i < n; ++i)
                    bb.setSample (c, i, (float) (b + 1) * 0.1f * (c == 0 ? 1.0f : -1.0f));
        }

        p.processBlock (buf, midi);

        auto out = p.getBusBuffer (buf, false, 0);
        const float expected = (float) (src + 1) * 0.1f;
        for (int i = 0; i < n; ++i)
        {
            REQUIRE (out.getSample (0, i) == expected);
            REQUIRE (out.getSample (1, i) == -expected);
        }
        for (int b = 0; b < MosaicAudioProcessor::kNumInputBuses; ++b)
        {
            REQUIRE (p.telemetry().inputEnabled[b].load());
            REQUIRE (p.telemetry().inputPeak[b].load() == (float) (b + 1) * 0.1f);
        }
    }
    p.releaseResources();
}

TEST_CASE ("mono main input is copied to both outputs", "[routing][buses]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MosaicAudioProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::mono());
    REQUIRE (p.checkBusesLayoutSupported (layout));
    REQUIRE (p.setBusesLayout (layout));
    p.setRateAndBufferSizeDetails (48000.0, 32);
    p.prepareToPlay (48000.0, 32);
    setChoice (p, params::probeSource, params::probeSourceInput);   // probe Input 1 (the engine is the default since M3)

    juce::AudioBuffer<float> buf (2, 32);
    buf.clear();
    for (int i = 0; i < 32; ++i) buf.setSample (0, i, 0.25f);
    juce::MidiBuffer midi;
    p.processBlock (buf, midi);
    for (int i = 0; i < 32; ++i)
    {
        REQUIRE (buf.getSample (0, i) == 0.25f);
        REQUIRE (buf.getSample (1, i) == 0.25f);
    }
}

TEST_CASE ("state round-trips role, group and send number", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MosaicAudioProcessor a;
    setChoice (a, params::role, 1);
    setChoice (a, params::sendGroup, 2);
    setChoice (a, params::sendNumber, 5);
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    MosaicAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    REQUIRE (b.state().getRawParameterValue (params::role)->load() == 1.0f);
    REQUIRE (b.state().getRawParameterValue (params::sendGroup)->load() == 2.0f);
    REQUIRE (b.state().getRawParameterValue (params::sendNumber)->load() == 5.0f);
    REQUIRE ((int) b.state().state.getProperty ("stateVersion") == MosaicAudioProcessor::kStateVersion);
}
