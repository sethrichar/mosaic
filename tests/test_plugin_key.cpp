// M4 plugin tests: the key pipeline through the real processor — detection on import,
// snap default, manual key changes re-render and swap, offline bounce waits for renders,
// RAM after a render, state round trip of key data without re-detection.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <thread>

#include "plugin/PluginProcessor.h"
#include "sources/FileLoader.h"

using mosaic::MosaicAudioProcessor;
namespace params = mosaic::params;
namespace analysis = mosaic::analysis;

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
            info.setPpqPosition ((double) timeInSamples / 24000.0);
            info.setTimeSignature (TimeSignature { 4, 4 });
            return info;
        }
    };

    // A short I-IV-V-I in a key, additive tones, written as a WAV.
    juce::File writeProgression (const juce::String& name, analysis::Key key, double seconds = 6.0)
    {
        const double sr = 48000.0;
        const int64_t n = (int64_t) (seconds * sr);
        std::vector<float> buf ((size_t) n, 0.0f);
        auto addNote = [&] (double t0, double len, int midi, float amp)
        {
            const double f = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
            const int64_t s = (int64_t) (t0 * sr), len_s = (int64_t) (len * sr);
            for (int64_t i = 0; i < len_s && s + i < n; ++i)
            {
                const double t = (double) i / sr;
                double v = 0.0;
                for (int h = 1; h <= 4; ++h) v += std::sin (2.0 * M_PI * f * h * t) / h;
                const double env = std::min ({ 1.0, (double) i / 480.0, (double) (len_s - i) / 2400.0 });
                buf[(size_t) (s + i)] += (float) (amp * env * v / 2.1);
            }
        };
        const int root = 48 + key.root;
        const int third = key.scale == analysis::Scale::Major ? 4 : 3;
        const int chords[4][3] = { { 0, third, 7 }, { 5, 5 + (key.scale == analysis::Scale::Major ? 4 : 3), 12 }, { 7, 11, 14 }, { 0, third, 7 } };
        const double chordLen = seconds / 4.0;
        for (int c = 0; c < 4; ++c)
            for (int rep = 0; rep < 4; ++rep)
            {
                const double t0 = c * chordLen + rep * chordLen / 4.0, len = chordLen / 4.0 * 0.95;
                addNote (t0, len, root - 12 + chords[c][0], 0.3f);
                for (int nn : chords[c]) addNote (t0, len, root + nn, 0.18f);
            }

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mosaic-tests");
        dir.createDirectory();
        auto file = dir.getChildFile (name);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> ab (1, (int) n);
        for (int64_t i = 0; i < n; ++i) ab.setSample (0, (int) i, buf[(size_t) i]);
        REQUIRE (writer->writeFromAudioSampleBuffer (ab, 0, (int) n));
        return file;
    }

    void setChoice (MosaicAudioProcessor& p, const juce::String& id, int index)
    {
        auto* param = p.state().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
    }

    void pumpUntil (MosaicAudioProcessor& p, std::function<bool()> done, int maxMs = 20000)
    {
        for (int i = 0; i < maxMs / 10 && ! done(); ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            p.applyMessageThreadUpdates();
        }
        REQUIRE (done());
    }

    std::vector<float> renderBlocks (MosaicAudioProcessor& p, FakePlayHead& ph, int blocks, int n = 512)
    {
        std::vector<float> out;
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            ph.timeInSamples = (int64_t) b * n;
            juce::AudioBuffer<float> buf (2, n);
            buf.clear();
            p.processBlock (buf, midi);
            for (int i = 0; i < n; ++i) out.push_back (buf.getSample (0, i));
        }
        return out;
    }

    float peakOf (const std::vector<float>& v) { float m = 0; for (float x : v) m = std::max (m, std::fabs (x)); return m; }
}

TEST_CASE ("import detects the key, defaults to Snap when confident, and Auto key follows slot 1", "[key][pipeline]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto fileA = writeProgression ("keyA.wav", { 9, analysis::Scale::Minor });   // A minor
    auto fileD = writeProgression ("keyD.wav", { 2, analysis::Scale::Major });   // D major

    FakePlayHead ph;
    MosaicAudioProcessor p;
    p.setPlayHead (&ph);
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    p.loadFile (0, fileA);
    p.loadFile (1, fileD);
    pumpUntil (p, [&] { return ! p.slotInfo (0).analyzing && ! p.slotInfo (1).analyzing && ! p.slotInfo (0).rendering && ! p.slotInfo (1).rendering && ! p.isBusy(); });

    const auto& a = p.slotInfo (0);
    const auto& d = p.slotInfo (1);
    REQUIRE (a.keyDetected); REQUIRE (a.key.found);
    REQUIRE (a.key.key.signaturePitchClass() == 0);                                   // A minor / C major family
    REQUIRE (d.keyDetected); REQUIRE (d.key.found);
    REQUIRE (d.key.key.signaturePitchClass() == 2);                                   // D major family

    bool known = false; int src = -1;
    const auto key = p.effectiveKey (known, &src);
    REQUIRE (known);
    REQUIRE (src == 0);                                                               // Auto: lowest confident slot
    REQUIRE (key.signaturePitchClass() == 0);

    // Slot 1 (D major) snaps down 2 semitones to the C signature; slot 0 needs nothing.
    REQUIRE (a.renderedShift == Catch::Approx (0.0));
    REQUIRE (p.state().getRawParameterValue (params::slotParam (1, params::slotKeyModeSuffix))->load() == 0.0f);   // Snap
    REQUIRE (d.desiredShift == Catch::Approx (-2.0));
    REQUIRE (d.renderedShift == Catch::Approx (-2.0));
    REQUIRE (d.bytes == a.bytes);                                                     // a snapped slot costs no more RAM than an unsnapped one
    REQUIRE (p.totalFileBytes() <= a.bytes + d.bytes + 1024);                         // and nothing extra is kept
}

TEST_CASE ("a manual key change re-renders and swaps the buffer during playback", "[key][pipeline]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto fileC = writeProgression ("keyC.wav", { 0, analysis::Scale::Major }, 4.0);

    FakePlayHead ph;
    MosaicAudioProcessor p;
    p.setPlayHead (&ph);
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setChoice (p, params::fade, 0);
    p.loadFile (0, fileC);
    pumpUntil (p, [&] { return ! p.slotInfo (0).analyzing && ! p.isBusy(); });
    REQUIRE (p.slotInfo (0).renderedShift == Catch::Approx (0.0));

    const auto original = renderBlocks (p, ph, 40);
    REQUIRE (peakOf (original) > 0.1f);

    // Manual key: G major -> +7 up or -5 down; Nearest picks -5.
    setChoice (p, params::keyMode, 1);
    setChoice (p, params::keyRoot, 7);
    setChoice (p, params::keyScale, 0);
    REQUIRE (p.desiredShiftFor (0) == Catch::Approx (-5.0));

    // The old buffer keeps playing until the render lands (then swaps when the slot is quiet
    // or after the wait limit); here the transport is stopped between renders, so immediately.
    pumpUntil (p, [&] { return p.slotInfo (0).renderedShift == Catch::Approx (-5.0) && ! p.isBusy(); });
    ph.playing = false;
    renderBlocks (p, ph, 1);          // stopped: pending swap happens now
    ph.playing = true;

    const auto shifted = renderBlocks (p, ph, 40);
    REQUIRE (peakOf (shifted) > 0.1f);
    REQUIRE (shifted != original);

    // It should equal a direct render of the same shift.
    auto src = mosaic::sources::FileLoader::loadNow (fileC, 48000.0, false);
    REQUIRE (src.ok);
    auto direct = mosaic::sources::FileLoader::renderNow (*src.buffer, -5.0);
    REQUIRE (direct.ok);
    // Bit-exact: the swapped buffer is the render, and 0 dB gains are exactly 1.0.
    REQUIRE (shifted.size() <= direct.buffer->left.size());
    REQUIRE (std::equal (shifted.begin(), shifted.end(), direct.buffer->left.begin()));

    // Direction Up: +7.
    setChoice (p, params::slotParam (0, params::slotDirectionSuffix), 1);
    REQUIRE (p.desiredShiftFor (0) == Catch::Approx (7.0));
    // Original mode: only transpose/fine.
    setChoice (p, params::slotParam (0, params::slotKeyModeSuffix), 1);
    REQUIRE (p.desiredShiftFor (0) == Catch::Approx (0.0));
    setChoice (p, params::slotParam (0, params::slotTransposeSuffix), 3);        // +3 semitones
    REQUIRE (p.desiredShiftFor (0) == Catch::Approx (3.0));
}

TEST_CASE ("an offline bounce started right after load waits for the render", "[key][pipeline][offline]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // this thread is the message thread
    auto fileE = writeProgression ("keyE.wav", { 4, analysis::Scale::Major }, 4.0);

    FakePlayHead ph;
    MosaicAudioProcessor p;
    p.setPlayHead (&ph);
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setChoice (p, params::keyMode, 1);          // manual C major: E major snaps -4
    setChoice (p, params::keyRoot, 0);
    setChoice (p, params::keyScale, 0);
    p.setNonRealtime (true);

    p.loadFile (0, fileE);
    // Pump just enough for the load + detection to be delivered (so the render is queued),
    // then bounce immediately: processBlock must wait for the render itself.
    pumpUntil (p, [&] { return p.slotInfo (0).keyDetected; });
    REQUIRE (p.slotInfo (0).rendering);

    // "Audio thread": a worker that bounces 20 blocks. Meanwhile this (message) thread keeps
    // pumping, as a host does during an offline bounce, so the render result can land.
    std::vector<float> out;
    std::atomic<bool> finished { false };
    std::thread audio ([&] { out = renderBlocks (p, ph, 20); finished = true; });
    while (! finished.load())
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        p.applyMessageThreadUpdates();
    }
    audio.join();

    REQUIRE (p.telemetry().offlineWaitsMs.load() > 0);        // it did wait
    REQUIRE (p.slotInfo (0).renderedShift == Catch::Approx (-4.0));
    REQUIRE (peakOf (out) > 0.1f);                              // and the bounce has audio, not silence
}

TEST_CASE ("state keeps key data and overrides, and restore does not re-detect", "[key][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto fileA = writeProgression ("keyA2.wav", { 9, analysis::Scale::Minor });

    MosaicAudioProcessor a;
    a.setRateAndBufferSizeDetails (48000.0, 256);
    a.prepareToPlay (48000.0, 256);
    REQUIRE (a.loadFileNow (0, fileA));
    REQUIRE (a.slotInfo (0).keyDetected);
    a.setSlotKeyOverride (0, { 6, analysis::Scale::Minor });   // user says F# minor
    setChoice (a, params::keyMode, 1);
    setChoice (a, params::keyRoot, 9);
    setChoice (a, params::keyScale, 1);
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    MosaicAudioProcessor b;
    b.setRateAndBufferSizeDetails (48000.0, 256);
    b.prepareToPlay (48000.0, 256);
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    REQUIRE ((int) b.state().state.getProperty ("stateVersion") == 3);
    REQUIRE (b.slotInfo (0).keyDetected);                                // restored, not re-run
    REQUIRE_FALSE (b.slotInfo (0).analyzing);
    REQUIRE (b.slotInfo (0).key.key == a.slotInfo (0).key.key);
    REQUIRE (b.slotInfo (0).key.confidence == a.slotInfo (0).key.confidence);
    REQUIRE (b.slotInfo (0).hasOverride);
    REQUIRE (b.slotInfo (0).overrideKey == analysis::Key { 6, analysis::Scale::Minor });
    bool known = false;
    REQUIRE (b.effectiveKey (known) == analysis::Key { 9, analysis::Scale::Minor });
    REQUIRE (known);
    // F# minor -> A minor: signatures A -> C, shift +3 nearest
    pumpUntil (b, [&] { return ! b.slotInfo (0).loading && ! b.slotInfo (0).rendering && ! b.isBusy(); });
    REQUIRE (b.slotInfo (0).renderedShift == Catch::Approx (3.0));
}
