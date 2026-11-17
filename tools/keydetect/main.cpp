// mosaic_keydetect: detects the key of every audio file in a folder and scores the result
// against an answer sheet (BUILD_PLAN M4 task 8, test-audio/key-set/README.md).
//
//   mosaic_keydetect <folder> [--csv keys.csv] [--profiles temperley|kk] [--no-confirm]
//
// Answer sheet, in priority order: a row in keys.csv (filename,key); else a key parsed
// from the file name ("..._Am.wav", "F#min", "Bb minor"); else "none".

#include <cstdio>
#include <map>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "analysis/KeyDetector.h"
#include "sources/FileLoader.h"

using namespace mosaic;

namespace
{
    struct Expected { bool known = false; bool none = false; analysis::Key key; juce::String from; };

    bool keyFromFileName (const juce::String& name, analysis::Key& out)
    {
        // Try every token split on space/underscore/hyphen/dot, longest first, plus pairs ("A minor").
        juce::StringArray tokens;
        tokens.addTokens (name.upToLastOccurrenceOf (".", false, false), " _-.()[]", "");
        tokens.removeEmptyStrings();
        for (int i = 0; i < tokens.size(); ++i)
        {
            if (i + 1 < tokens.size() && analysis::Key::parse ((tokens[i] + " " + tokens[i + 1]).toStdString(), out))
            {
                const auto t2 = tokens[i + 1].toLowerCase();
                if (t2 == "min" || t2 == "minor" || t2 == "maj" || t2 == "major" || t2 == "m")
                    return true;
            }
        }
        for (const auto& t : tokens)
        {
            // A bare letter like "A" is too ambiguous in a file name; require a modifier or accidental.
            const auto lower = t.toLowerCase();
            const bool bare = lower.length() == 1;
            if (! bare && analysis::Key::parse (t.toStdString(), out))
                return true;
        }
        return false;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::printf ("usage: mosaic_keydetect <folder> [--csv keys.csv] [--profiles temperley|kk] [--no-confirm]\n");
        return 2;
    }

    const juce::File folder { juce::String (argv[1]) };
    juce::File csv = folder.getChildFile ("keys.csv");
    analysis::KeyDetectorSettings settings;
    bool confirm = true;
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--csv" && i + 1 < argc) csv = juce::File { juce::String (argv[++i]) };
        else if (a == "--profiles" && i + 1 < argc)
        {
            const juce::String p (argv[++i]);
            settings.profiles = p == "kk" ? &analysis::KeyProfiles::krumhanslKessler() : &analysis::KeyProfiles::temperley();
        }
        else if (a == "--no-confirm") confirm = false;
    }

    // Answer sheet
    std::map<juce::String, Expected> expected;
    if (csv.existsAsFile())
    {
        juce::StringArray lines;
        csv.readLines (lines);
        for (const auto& line : lines)
        {
            if (line.trim().isEmpty() || line.startsWithIgnoreCase ("filename")) continue;
            const int comma = line.lastIndexOfChar (',');
            if (comma < 0) continue;
            const auto name = line.substring (0, comma).trim();
            const auto keyText = line.substring (comma + 1).trim();
            Expected e; e.known = true; e.from = "keys.csv";
            if (keyText.equalsIgnoreCase ("none")) e.none = true;
            else if (! analysis::Key::parse (keyText.toStdString(), e.key)) { std::printf ("keys.csv: cannot parse key '%s' for %s\n", keyText.toRawUTF8(), name.toRawUTF8()); continue; }
            expected[name] = e;
        }
    }

    juce::Array<juce::File> files;
    folder.findChildFiles (files, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    files.sort();
    if (files.isEmpty())
    {
        std::printf ("no audio files in %s\n", folder.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("Answer sheet (%d files):\n", files.size());
    std::vector<Expected> exp;
    for (const auto& f : files)
    {
        Expected e;
        auto it = expected.find (f.getFileName());
        if (it != expected.end()) e = it->second;
        else if (keyFromFileName (f.getFileName(), e.key)) { e.known = true; e.from = "file name"; }
        else { e.known = true; e.none = true; e.from = "no key in name"; }
        exp.push_back (e);
        std::printf ("  %-50s %-8s (%s)\n", f.getFileName().toRawUTF8(), e.none ? "none" : e.key.name().c_str(), e.from.toRawUTF8());
    }
    if (confirm)
        std::printf ("\nCheck the sheet above; rerun with --no-confirm or fix keys.csv if any line is wrong.\n\n");

    int pitched = 0, exact = 0, sameSig = 0, unpitched = 0, unpitchedHigh = 0;
    std::vector<juce::String> misses, disagreements;
    std::printf ("%-50s %-10s %-10s %5s %6s %6s  %s\n", "file", "expected", "detected", "conf", "tonal", "tune", "verdict");
    for (int i = 0; i < files.size(); ++i)
    {
        const auto r = sources::FileLoader::loadNow (files[i], 0.0, false);
        if (! r.ok) { std::printf ("%-50s load failed: %s\n", files[i].getFileName().toRawUTF8(), r.error.toRawUTF8()); continue; }
        const auto k = analysis::KeyDetector::analyze (r.buffer->left.data(), r.buffer->right.data(), r.buffer->numSamples, r.buffer->sampleRate, settings);
        const auto& e = exp[(size_t) i];
        juce::String detected = k.found ? juce::String (k.key.name()) + (k.level == analysis::Confidence::High ? "" : "?") : "none";
        juce::String verdict;
        if (e.none)
        {
            ++unpitched;
            if (k.found && k.level == analysis::Confidence::High) { ++unpitchedHigh; verdict = "UNPITCHED BUT HIGH CONFIDENCE"; }
            else verdict = "ok";
        }
        else
        {
            ++pitched;
            if (k.found && k.key == e.key) { ++exact; ++sameSig; verdict = "exact"; }
            else if (k.found && k.key.signaturePitchClass() == e.key.signaturePitchClass()) { ++sameSig; verdict = "relative"; }
            else { verdict = "MISS"; misses.push_back (files[i].getFileName() + ": expected " + e.key.name() + ", got " + detected); }
            if (verdict != "exact") disagreements.push_back (files[i].getFileName() + ": label " + e.key.name() + " (" + e.from + "), detector " + detected);
        }
        std::printf ("%-50s %-10s %-10s %5.2f %6.2f %+5.0f  %s\n", files[i].getFileName().toRawUTF8(),
                     e.none ? "none" : e.key.name().c_str(), detected.toRawUTF8(), k.confidence, k.tonalness, k.tuningCents, verdict.toRawUTF8());
    }

    std::printf ("\nPitched files: %d   exact %d (%.0f%%)   same signature %d (%.0f%%)   target: >= 75%% same signature\n",
                 pitched, exact, pitched ? 100.0 * exact / pitched : 0.0, sameSig, pitched ? 100.0 * sameSig / pitched : 0.0);
    std::printf ("Unpitched files: %d   reported with high confidence: %d   target: 0\n", unpitched, unpitchedHigh);
    if (! misses.empty())
    {
        std::printf ("\nMisses:\n");
        for (const auto& m : misses) std::printf ("  %s\n", m.toRawUTF8());
    }
    if (! disagreements.empty())
    {
        std::printf ("\nDetector vs label (judge by ear; sample-pack labels are usually right but not always):\n");
        for (const auto& d : disagreements) std::printf ("  %s\n", d.toRawUTF8());
    }
    return 0;
}
