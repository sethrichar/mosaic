#pragma once

// Musical key types shared by the detector, the snap math and the plugin. Plain C++.

#include <string>

namespace mosaic::analysis
{

enum class Scale : int { Major = 0, Minor = 1 };

struct Key
{
    int   root  = 0;             ///< pitch class 0 = C … 11 = B
    Scale scale = Scale::Major;

    bool operator== (const Key& o) const noexcept { return root == o.root && scale == o.scale; }
    bool operator!= (const Key& o) const noexcept { return ! (*this == o); }

    /// Pitch class of the key signature: a major key is its root, a minor key is its
    /// relative major (root + 3). Relative keys share a signature (SPEC 7.4).
    int signaturePitchClass() const noexcept { return scale == Scale::Major ? root : (root + 3) % 12; }

    /// Stable index 0–23 for tables and state: major 0–11, minor 12–23.
    int toIndex() const noexcept { return (scale == Scale::Minor ? 12 : 0) + root; }
    static Key fromIndex (int i) noexcept
    {
        i = ((i % 24) + 24) % 24;
        return { i % 12, i >= 12 ? Scale::Minor : Scale::Major };
    }

    static const char* rootName (int root, bool flats = false) noexcept
    {
        static const char* sharps[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* flatsN[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        root = ((root % 12) + 12) % 12;
        return flats ? flatsN[root] : sharps[root];
    }

    /// "F# min", "C maj"
    std::string name() const { return std::string (rootName (root)) + (scale == Scale::Minor ? " min" : " maj"); }

    /// Parses "Am", "Amin", "A minor", "A min", "Cmaj", "C major", "C", "F#", "Gb", "Bbm",
    /// "F#m", "f#min" (case-insensitive, separators ignored). A bare root is major.
    static bool parse (const std::string& text, Key& out);
};

} // namespace mosaic::analysis
