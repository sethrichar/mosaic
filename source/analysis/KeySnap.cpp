#include "analysis/KeySnap.h"

#include <algorithm>
#include <cctype>

namespace mosaic::analysis
{

int KeySnap::shiftSemitones (Key source, Key target, Direction direction) noexcept
{
    const int up = ((target.signaturePitchClass() - source.signaturePitchClass()) % 12 + 12) % 12;   // 0..11

    switch (direction)
    {
        case Direction::Up:      return up;
        case Direction::Down:    return up == 0 ? 0 : up - 12;
        case Direction::Nearest:
        default:                 return up > 5 ? up - 12 : up;
    }
}

bool Key::parse (const std::string& text, Key& out)
{
    // Normalise: lower-case, strip spaces, underscores and hyphens.
    std::string s;
    for (char ch : text)
        if (! std::isspace ((unsigned char) ch) && ch != '_' && ch != '-')
            s += (char) std::tolower ((unsigned char) ch);
    if (s.empty()) return false;

    const char letter = s[0];
    if (letter < 'a' || letter > 'g') return false;
    static const int naturals[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    int root = naturals[letter - 'a'];
    size_t i = 1;

    auto startsWith = [&s] (size_t at, const char* word)
    {
        const std::string w (word);
        return s.compare (at, w.size(), w) == 0;
    };

    if (i < s.size())
    {
        if (startsWith (i, "sharp"))        { root = (root + 1) % 12;  i += 5; }
        else if (startsWith (i, "flat"))    { root = (root + 11) % 12; i += 4; }
        else if (s[i] == '#' || s[i] == 's') { root = (root + 1) % 12; ++i; }
        else if (s[i] == 'b')
        {
            // A 'b' right after the letter is a flat ("Gb", "Bbm", "Bbmaj") unless the
            // rest of the string is itself a complete mode word ("b" + "min" = B minor).
            const std::string rest = s.substr (i);
            const bool restIsMode = rest == "m" || rest == "min" || rest == "minor" || rest == "mi"
                                 || rest == "maj" || rest == "major";
            if (! restIsMode) { root = (root + 11) % 12; ++i; }
        }
    }

    const std::string mode = s.substr (i);
    if (mode.empty() || mode == "maj" || mode == "major" || mode == "M")
        out = { root, Scale::Major };
    else if (mode == "m" || mode == "min" || mode == "minor" || mode == "mi")
        out = { root, Scale::Minor };
    else
        return false;
    return true;
}

} // namespace mosaic::analysis
