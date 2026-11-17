// M3 plugin tests: real file loading, state round-trip with file paths, missing files,
// and render determinism through the whole plugin (engine + parameters + output gain).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "plugin/PluginProcessor.h"
#include "sources/FileLoader.h"

using mosaic::MosaicAudioProcessor;
namespace params = mosaic::params;

namespace
{
    struct FakePlayHead final : juce::AudioPlayHead
    {
        int64_t timeInSamples = 0;
        bool playing = true;
        double sampleRate = 48000.0, bpm = 120.0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setTimeInSamples (timeInSamples);
            info.setIsPlaying (playing);
            info.setBpm (bpm);
            info.setPpqPosition ((double) timeInSamples / (sampleRate * 60.0 / bpm));
            info.setTimeSignature (TimeSignature { 4, 4 });
            return info;
        }
    };

    // Writes a WAV: channels of a slow ramp (exact in float), plus a tag per channel.
    juce::File writeWav (const juce::String& name, double sampleRate, int channels, int length)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mosaic-tests");
        dir.createDirectory();
        auto file = dir.getChildFile (name);
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                       .withSampleRate (sampleRate)
                                                       .withNumChannels (channels)
                                                       .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> buf (channels, length);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < length; ++i)
                buf.setSample (c, i, 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * (float) i / 100.0f) * (c == 0 ? 1.0f : -0.5f));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, length));
        writer.reset();
        return file;
    }

    void setChoice (MosaicAudioProcessor& p, const juce::String& id, int index)
    {
        auto* param = p.state().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
    }
    void setFloat (MosaicAudioProcessor& p, const juce::String& id, float v)
    {
        auto* param = p.state().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    }

    std::vector<float> renderSpan (MosaicAudioProcessor& p, FakePlayHead& ph, int64_t start, int64_t end,
                                   const std::vector<int>& pattern, bool offline = false)
    {
        std::vector<float> out;
        juce::MidiBuffer midi;
        p.setNonRealtime (offline);
        int64_t t = start; size_t pi = 0;
        while (t < end)
        {
            const int n = (int) std::min<int64_t> (pattern[pi++ % pattern.size()], end - t);
            ph.timeInSamples = t;
            juce::AudioBuffer<float> buf (2, n);
            buf.clear();
            p.processBlock (buf, midi);
            for (int i = 0; i < n; ++i) { out.push_back (buf.getSample (0, i)); out.push_back (buf.getSample (1, i)); }
            t += n;
        }
        return out;
    }
}

TEST_CASE ("FileLoader decodes, downmixes and resamples", "[files]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("same rate is a bit-exact copy, mono goes to both channels")
    {
        auto f = writeWav ("mono44.wav", 44100.0, 1, 1000);
        auto r = mosaic::sources::FileLoader::loadNow (f, 44100.0);
        REQUIRE (r.ok);
        REQUIRE (r.buffer->numSamples == 1000);
        REQUIRE (r.sourceChannels == 1);
        REQUIRE (r.buffer->left == r.buffer->right);
        REQUIRE (std::fabs (r.buffer->left[25] - 0.5f) < 2.0e-4f);   // 24-bit quantisation only
        REQUIRE (r.overview != nullptr);
        REQUIRE (r.overview->size() == 2 * mosaic::sources::FileLoader::kOverviewBins);
    }
    SECTION ("stereo is preserved, extra channels are dropped")
    {
        auto f = writeWav ("quad48.wav", 48000.0, 4, 500);
        auto r = mosaic::sources::FileLoader::loadNow (f, 48000.0);
        REQUIRE (r.ok);
        REQUIRE (r.sourceChannels == 4);
        REQUIRE (std::fabs (r.buffer->right[25] + 0.25f) < 2.0e-4f);
    }
    SECTION ("resampling keeps length proportional and content intact")
    {
        auto f = writeWav ("mono44b.wav", 44100.0, 1, 44100);
        auto r = mosaic::sources::FileLoader::loadNow (f, 48000.0);
        REQUIRE (r.ok);
        REQUIRE (std::abs (r.buffer->numSamples - 48000) <= 1);
        REQUIRE (r.buffer->sampleRate == 48000.0);
        float peak = 0.0f;
        for (int64_t i = 1000; i < 47000; ++i) peak = std::max (peak, std::fabs (r.buffer->left[(size_t) i]));
        REQUIRE (peak > 0.45f); REQUIRE (peak < 0.55f);
    }
    SECTION ("missing and unreadable files fail cleanly")
    {
        auto r = mosaic::sources::FileLoader::loadNow (juce::File ("/nonexistent/thing.wav"), 48000.0);
        REQUIRE_FALSE (r.ok);
        auto bad = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mosaic-tests/bad.wav");
        bad.replaceWithText ("not audio");
        r = mosaic::sources::FileLoader::loadNow (bad, 48000.0);
        REQUIRE_FALSE (r.ok);
    }
}

TEST_CASE ("state round-trips parameters and file paths, and the restored instance sounds identical", "[state][files]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto fa = writeWav ("slotA.wav", 48000.0, 2, 48000);
    auto fb = writeWav ("slotB.wav", 44100.0, 1, 30000);

    FakePlayHead ph;
    MosaicAudioProcessor a;
    a.setPlayHead (&ph);
    a.setRateAndBufferSizeDetails (48000.0, 512);
    a.prepareToPlay (48000.0, 512);
    REQUIRE (a.loadFileNow (0, fa));
    REQUIRE (a.loadFileNow (3, fb));
    setChoice (a, params::rate, mosaic::engine::Rate { mosaic::engine::RateBase::Sixteenth, mosaic::engine::RateMod::Straight }.toIndex());
    setFloat (a, params::fade, 3.0f);
    setFloat (a, params::seed, 777.0f);
    setFloat (a, params::outputGain, -6.0f);
    setFloat (a, params::slotParam (3, params::slotStartOffsetSuffix), 0.5f);
    setChoice (a, params::avoidRepeats, 1);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    MosaicAudioProcessor b;
    b.setPlayHead (&ph);
    b.setRateAndBufferSizeDetails (48000.0, 512);
    b.prepareToPlay (48000.0, 512);
    b.setStateInformation (blob.getData(), (int) blob.getSize());

    REQUIRE (b.slotInfo (0).file == fa);
    REQUIRE (b.slotInfo (3).file == fb);
    REQUIRE_FALSE (b.slotInfo (1).hasFile());
    REQUIRE (b.state().getRawParameterValue (params::seed)->load() == 777.0f);
    REQUIRE (b.state().getRawParameterValue (params::outputGain)->load() == Catch::Approx (-6.0f).margin (1e-4));
    REQUIRE ((int) b.state().state.getProperty ("stateVersion") == MosaicAudioProcessor::kStateVersion);

    // The restored instance loads on the background thread; wait for it, then compare renders.
    for (int i = 0; i < 500 && (b.slotInfo (0).loading || b.slotInfo (3).loading); ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        b.applyMessageThreadUpdates();
    }
    REQUIRE_FALSE (b.slotInfo (0).loading);
    REQUIRE (b.slotInfo (0).lengthSamples == 48000);
    REQUIRE (b.slotInfo (3).lengthSamples == a.slotInfo (3).lengthSamples);
    a.applyMessageThreadUpdates();

    const auto ra = renderSpan (a, ph, 0, 96000, { 512 });
    const auto rb = renderSpan (b, ph, 0, 96000, { 512 });
    REQUIRE (ra == rb);
    float peak = 0.0f;
    for (float v : ra) peak = std::max (peak, std::fabs (v));
    REQUIRE (peak > 0.1f);   // it actually played something
}

TEST_CASE ("a missing file restores as 'missing' and the other slots still load", "[state][files]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto fa = writeWav ("present.wav", 48000.0, 1, 2000);

    MosaicAudioProcessor a;
    a.setRateAndBufferSizeDetails (48000.0, 256);
    a.prepareToPlay (48000.0, 256);
    REQUIRE (a.loadFileNow (0, fa));
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    // Tamper with the saved path of slot 2.
    auto tree = juce::ValueTree::readFromData (blob.getData(), blob.getSize());
    auto files = tree.getChildWithName ("files");
    juce::ValueTree gone ("slot");
    gone.setProperty ("index", 2, nullptr);
    gone.setProperty ("path", "/definitely/not/here.wav", nullptr);
    files.appendChild (gone, nullptr);
    juce::MemoryBlock blob2;
    { juce::MemoryOutputStream out (blob2, false); tree.writeToStream (out); }

    MosaicAudioProcessor b;
    b.setRateAndBufferSizeDetails (48000.0, 256);
    b.prepareToPlay (48000.0, 256);
    b.setStateInformation (blob2.getData(), (int) blob2.getSize());
    REQUIRE (b.slotInfo (2).missing);
    REQUIRE (b.slotInfo (2).file.getFileName() == "here.wav");
    for (int i = 0; i < 500 && b.slotInfo (0).loading; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        b.applyMessageThreadUpdates();
    }
    REQUIRE_FALSE (b.slotInfo (0).missing);
    REQUIRE (b.slotInfo (0).lengthSamples == 2000);

    FakePlayHead ph;
    b.setPlayHead (&ph);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, 256);
    b.processBlock (buf, midi);   // nothing crashes with a missing slot
}

TEST_CASE ("offline bounces and variable block sizes are bit-identical through the plugin", "[determinism]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto fa = writeWav ("detA.wav", 48000.0, 2, 24000);
    auto fb = writeWav ("detB.wav", 48000.0, 1, 50000);

    FakePlayHead ph;
    MosaicAudioProcessor p;
    p.setPlayHead (&ph);
    p.setRateAndBufferSizeDetails (48000.0, 1024);
    p.prepareToPlay (48000.0, 1024);
    REQUIRE (p.loadFileNow (1, fa));
    REQUIRE (p.loadFileNow (5, fb));
    setChoice (p, params::position, 1);          // Free
    setFloat (p, params::tileLength, 70.0f);
    setChoice (p, params::patternLength, 2);     // 2 bars
    p.applyMessageThreadUpdates();

    const auto first  = renderSpan (p, ph, 0, 48000 * 4, { 1024 }, true);
    const auto second = renderSpan (p, ph, 0, 48000 * 4, { 1024 }, true);
    REQUIRE (first == second);

    const auto varied = renderSpan (p, ph, 0, 48000 * 4, { 1, 33, 1024, 7, 500, 2048 }, false);   // 2048 > prepared
    REQUIRE (first == varied);

    const auto fromBar2 = renderSpan (p, ph, 96000, 48000 * 4, { 1024 }, false);
    REQUIRE (std::equal (fromBar2.begin(), fromBar2.end(), first.begin() + 96000 * 2));
}

TEST_CASE ("muted slots and output gain behave", "[params]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto fa = writeWav ("gainA.wav", 48000.0, 1, 48000);
    FakePlayHead ph;
    MosaicAudioProcessor p;
    p.setPlayHead (&ph);
    p.setRateAndBufferSizeDetails (48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    REQUIRE (p.loadFileNow (0, fa));
    setFloat (p, params::fade, 0.0f);

    auto peakOf = [] (const std::vector<float>& v) { float m = 0; for (float x : v) m = std::max (m, std::fabs (x)); return m; };

    const auto unity = renderSpan (p, ph, 0, 24000, { 256 });
    REQUIRE (peakOf (unity) > 0.45f);

    setFloat (p, params::outputGain, -6.0f);
    const auto quieter = renderSpan (p, ph, 0, 24000, { 256 });
    REQUIRE (std::fabs (peakOf (quieter) - peakOf (unity) * 0.5012f) < 0.01f);

    setFloat (p, params::outputGain, 0.0f);
    setChoice (p, params::slotParam (0, params::slotMuteSuffix), 1);
    REQUIRE (peakOf (renderSpan (p, ph, 0, 24000, { 256 })) == 0.0f);   // only slot muted: silence

    setChoice (p, params::slotParam (0, params::slotMuteSuffix), 0);
    setFloat (p, params::slotParam (0, params::slotGainSuffix), params::kMinGainDb);
    REQUIRE (peakOf (renderSpan (p, ph, 0, 24000, { 256 })) == 0.0f);   // -60 dB = silence
}
