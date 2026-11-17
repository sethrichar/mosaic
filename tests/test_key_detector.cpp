#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "analysis/KeyDetector.h"

using namespace mosaic::analysis;

namespace
{
    constexpr double kSr = 44100.0;

    // Additive tone with a few harmonics, short attack/release to avoid clicks.
    void addNote (std::vector<float>& buf, double startSec, double lenSec, int midi, float amp = 0.3f)
    {
        const double f = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        const int64_t s = (int64_t) (startSec * kSr), n = (int64_t) (lenSec * kSr);
        for (int64_t i = 0; i < n && s + i < (int64_t) buf.size(); ++i)
        {
            const double t = (double) i / kSr;
            double v = 0.0;
            for (int h = 1; h <= 5; ++h) v += std::sin (2.0 * M_PI * f * h * t) / h;
            const double env = std::min ({ 1.0, (double) i / 441.0, (double) (n - i) / 2205.0 });
            buf[(size_t) (s + i)] += (float) (amp * env * v / 2.3);
        }
    }

    // I–IV–V–I in a major key or i–iv–V–i in (harmonic) minor, two octaves, bass + chord.
    std::vector<float> progression (Key key, double seconds = 8.0)
    {
        std::vector<float> buf ((size_t) (seconds * kSr), 0.0f);
        const int root = 48 + key.root;
        const bool major = key.scale == Scale::Major;
        const int third = major ? 4 : 3;
        const int chords[4][3] = {
            { 0, third, 7 },                        // I / i
            { 5, 5 + (major ? 4 : 3), 12 },         // IV / iv
            { 7, 7 + 4, 14 },                       // V (major in both)
            { 0, third, 7 },
        };
        const double chordLen = seconds / 4.0;
        for (int c = 0; c < 4; ++c)
            for (int rep = 0; rep < 4; ++rep)
            {
                const double t0 = c * chordLen + rep * chordLen / 4.0, len = chordLen / 4.0 * 0.95;
                addNote (buf, t0, len, root - 12 + chords[c][0], 0.35f);   // bass
                for (int n : chords[c]) addNote (buf, t0, len, root + n, 0.2f);
            }
        return buf;
    }

    std::vector<float> scaleRun (Key key, double seconds = 6.0)
    {
        std::vector<float> buf ((size_t) (seconds * kSr), 0.0f);
        const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 }, minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int* steps = key.scale == Scale::Major ? majorSteps : minorSteps;
        const int root = 60 + key.root;
        int idx = 0;
        for (double t = 0.0; t < seconds; t += 0.25, ++idx)
            addNote (buf, t, 0.24, root + steps[idx % 7] + 12 * ((idx / 7) % 2), 0.3f);
        // Tonic emphasis so the mode is unambiguous.
        for (double t = 0.0; t < seconds; t += 2.0) addNote (buf, t, 1.0, root - 12, 0.3f);
        return buf;
    }
}

TEST_CASE ("chord progressions in all 24 keys are identified", "[keydetect]")
{
    int exact = 0, sameSignature = 0, high = 0;
    std::string report;
    for (int i = 0; i < 24; ++i)
    {
        const Key key = Key::fromIndex (i);
        auto buf = progression (key);
        const auto r = KeyDetector::analyze (buf.data(), nullptr, (int64_t) buf.size(), kSr);
        report += key.name() + " -> " + r.key.name() + " conf " + std::to_string (r.confidence)
                + " margin " + std::to_string (r.margin) + " tonal " + std::to_string (r.tonalness)
                + (r.key == key ? "" : "   <-- MISS") + "\n";
        REQUIRE (r.found);
        if (r.key == key) ++exact;
        if (r.key.signaturePitchClass() == key.signaturePitchClass()) ++sameSignature;
        if (r.level == Confidence::High) ++high;
    }
    INFO (report);
    REQUIRE (sameSignature == 24);
    REQUIRE (exact >= 22);
    REQUIRE (high >= 20);
}

TEST_CASE ("scale runs land on the right signature", "[keydetect]")
{
    for (int i = 0; i < 24; ++i)
    {
        const Key key = Key::fromIndex (i);
        auto buf = scaleRun (key);
        const auto r = KeyDetector::analyze (buf.data(), buf.data(), (int64_t) buf.size(), kSr);
        INFO ("expected " << key.name() << " got " << r.key.name() << " conf " << r.confidence
              << " score " << r.bestScore << " margin " << r.margin << " tonal " << r.tonalness);
        REQUIRE (r.found);
        REQUIRE (r.key.signaturePitchClass() == key.signaturePitchClass());
    }
}

TEST_CASE ("white noise and drum-like bursts come back as none", "[keydetect]")
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);

    std::vector<float> noise ((size_t) (6.0 * kSr));
    for (auto& v : noise) v = dist (rng);
    auto r = KeyDetector::analyze (noise.data(), nullptr, (int64_t) noise.size(), kSr);
    INFO ("noise conf " << r.confidence << " tonal " << r.tonalness);
    REQUIRE_FALSE (r.found);
    REQUIRE (r.level == Confidence::None);

    // Drums: short decaying noise bursts at 120 BPM with a low thump.
    std::vector<float> drums ((size_t) (6.0 * kSr), 0.0f);
    for (double t = 0.0; t < 6.0; t += 0.5)
    {
        const int64_t s = (int64_t) (t * kSr);
        for (int64_t i = 0; i < 4410 && s + i < (int64_t) drums.size(); ++i)
        {
            const double env = std::exp (-(double) i / 800.0);
            drums[(size_t) (s + i)] += (float) (env * (dist (rng) + 0.6 * std::sin (2.0 * M_PI * 60.0 * std::exp (-i / 2000.0) * i / kSr)));
        }
    }
    r = KeyDetector::analyze (drums.data(), nullptr, (int64_t) drums.size(), kSr);
    INFO ("drums conf " << r.confidence << " tonal " << r.tonalness << " key " << r.key.name());
    REQUIRE (r.level != Confidence::High);

    std::vector<float> silence ((size_t) kSr, 0.0f);
    r = KeyDetector::analyze (silence.data(), nullptr, (int64_t) silence.size(), kSr);
    REQUIRE_FALSE (r.found);
}

TEST_CASE ("detuned material still resolves and reports its tuning", "[keydetect]")
{
    // A minor progression played 30 cents sharp.
    Key key { 9, Scale::Minor };
    auto buf = progression (key);
    // Resample by a pitch factor via naive linear interpolation (good enough for a test).
    const double factor = std::pow (2.0, 0.30 / 12.0);
    std::vector<float> sharp ((size_t) ((double) buf.size() / factor));
    for (size_t i = 0; i < sharp.size(); ++i)
    {
        const double p = (double) i * factor;
        const size_t a = (size_t) p; const double fr = p - (double) a;
        sharp[i] = a + 1 < buf.size() ? (float) ((1.0 - fr) * buf[a] + fr * buf[a + 1]) : 0.0f;
    }
    const auto r = KeyDetector::analyze (sharp.data(), nullptr, (int64_t) sharp.size(), kSr);
    INFO ("got " << r.key.name() << " tuning " << r.tuningCents << " conf " << r.confidence);
    REQUIRE (r.found);
    REQUIRE (r.key.signaturePitchClass() == key.signaturePitchClass());
    REQUIRE (std::fabs (r.tuningCents - 30.0f) < 12.0f);
}

TEST_CASE ("detection is deterministic and works at other sample rates", "[keydetect]")
{
    Key key { 4, Scale::Major };
    auto buf = progression (key);
    const auto a = KeyDetector::analyze (buf.data(), nullptr, (int64_t) buf.size(), kSr);
    const auto b = KeyDetector::analyze (buf.data(), nullptr, (int64_t) buf.size(), kSr);
    REQUIRE (a.confidence == b.confidence);
    REQUIRE (a.key == b.key);
    for (int i = 0; i < 12; ++i) REQUIRE (a.chroma[i] == b.chroma[i]);

    // Pretend the same samples are 96 kHz: everything is an octave and a bit lower, same key class shift.
    const auto c = KeyDetector::analyze (buf.data(), nullptr, (int64_t) buf.size(), 96000.0);
    REQUIRE (c.found);
}

TEST_CASE ("scoreChroma: a pure major triad profile picks the major key", "[keydetect]")
{
    float chroma[12] = {};
    chroma[0] = 5; chroma[4] = 3; chroma[7] = 4; chroma[2] = 1; chroma[5] = 1; chroma[9] = 1; chroma[11] = 1;   // C major-ish
    const auto r = KeyDetector::scoreChroma (chroma);
    REQUIRE (r.key == Key { 0, Scale::Major });
    REQUIRE (r.found);
    float flat[12]; for (auto& v : flat) v = 1.0f;
    REQUIRE_FALSE (KeyDetector::scoreChroma (flat).found);
}

TEST_CASE ("experiment: detector weighting matrix", "[.][experiment]")
{
    struct Config { const char* name; KeyDetectorSettings s; };
    std::vector<Config> configs;
    auto base = KeyDetectorSettings {};
    configs.push_back ({ "default", base });
    { auto c = base; c.profiles = &KeyProfiles::krumhanslKessler(); c.bassEmphasis = 0.0f; configs.push_back ({ "KK sqrt", c }); }
    { auto c = base; c.profiles = &KeyProfiles::temperley(); configs.push_back ({ "Temperley sqrt", c }); }
    { auto c = base; c.weightExponent = 0.25f; configs.push_back ({ "KK ^0.25", c }); }
    { auto c = base; c.weightExponent = 1.0f; configs.push_back ({ "KK linear", c }); }
    { auto c = base; c.bassEmphasis = 1.0f; configs.push_back ({ "KK bass1", c }); }
    { auto c = base; c.bassEmphasis = 3.0f; configs.push_back ({ "KK bass3", c }); }
    { auto c = base; c.subharmonics = 0.5f; configs.push_back ({ "KK sub0.5", c }); }
    { auto c = base; c.subharmonics = 1.0f; configs.push_back ({ "KK sub1", c }); }
    { auto c = base; c.maxFreqHz = 1100.0; configs.push_back ({ "KK <1.1k", c }); }
    { auto c = base; c.profiles = &KeyProfiles::temperley(); c.bassEmphasis = 1.0f; configs.push_back ({ "Temp bass1", c }); }
    { auto c = base; c.profiles = &KeyProfiles::temperley(); c.subharmonics = 0.5f; configs.push_back ({ "Temp sub0.5", c }); }
    { auto c = base; c.profiles = &KeyProfiles::temperley(); c.weightExponent = 0.25f; c.bassEmphasis = 1.0f; configs.push_back ({ "Temp ^.25 bass1", c }); }

    std::mt19937 rng (7);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    std::vector<float> noise ((size_t) (6.0 * kSr));
    for (auto& v : noise) v = dist (rng);

    std::printf ("%-18s %5s %5s %5s | %5s %5s | %6s %6s | %6s\n", "config", "exact", "sig", "high", "sExct", "sSig", "cfMaj", "cfMin", "noise");
    for (const auto& cfg : configs)
    {
        int exact = 0, sig = 0, high = 0, sExact = 0, sSig = 0;
        double cfMaj = 0, cfMin = 0;
        for (int i = 0; i < 24; ++i)
        {
            const Key key = Key::fromIndex (i);
            auto buf = progression (key);
            const auto r = KeyDetector::analyze (buf.data(), nullptr, (int64_t) buf.size(), kSr, cfg.s);
            exact += r.key == key; sig += r.key.signaturePitchClass() == key.signaturePitchClass(); high += r.level == Confidence::High;
            (key.scale == Scale::Major ? cfMaj : cfMin) += r.confidence / 12.0;
            auto sc = scaleRun (key);
            const auto rs = KeyDetector::analyze (sc.data(), nullptr, (int64_t) sc.size(), kSr, cfg.s);
            sExact += rs.key == key; sSig += rs.key.signaturePitchClass() == key.signaturePitchClass();
        }
        const auto rn = KeyDetector::analyze (noise.data(), nullptr, (int64_t) noise.size(), kSr, cfg.s);
        std::printf ("%-18s %5d %5d %5d | %5d %5d | %6.3f %6.3f | %6.3f\n", cfg.name, exact, sig, high, sExact, sSig, cfMaj, cfMin, rn.confidence);
    }
}
