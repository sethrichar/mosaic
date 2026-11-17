#pragma once

// Grid rate: tile length as a musical subdivision (SPEC 4.2). Plain C++.

namespace mosaic::engine
{

enum class RateBase : int { Whole = 0, Half, Quarter, Eighth, Sixteenth, ThirtySecond };
enum class RateMod  : int { Straight = 0, Dotted, Triplet };

struct Rate
{
    RateBase base = RateBase::Eighth;
    RateMod  mod  = RateMod::Straight;

    static constexpr int kNumBases = 6, kNumMods = 3, kNumChoices = kNumBases * kNumMods;

    /// Length of one step in quarter notes.
    constexpr double lengthInQuarterNotes() const noexcept
    {
        double qn = 4.0 / (double) (1 << (int) base);   // whole = 4 … 32nd = 0.125
        if (mod == RateMod::Dotted)  qn *= 1.5;
        if (mod == RateMod::Triplet) qn *= 2.0 / 3.0;
        return qn;
    }

    /// Stable index for parameters and state: base-major, 0 … kNumChoices-1.
    constexpr int toIndex() const noexcept { return (int) base * kNumMods + (int) mod; }

    static constexpr Rate fromIndex (int index) noexcept
    {
        if (index < 0) index = 0;
        if (index >= kNumChoices) index = kNumChoices - 1;
        return { (RateBase) (index / kNumMods), (RateMod) (index % kNumMods) };
    }

    constexpr bool operator== (const Rate& o) const noexcept { return base == o.base && mod == o.mod; }
    constexpr bool operator!= (const Rate& o) const noexcept { return ! (*this == o); }

    /// "1/8", "1/8 D", "1/8 T"
    static const char* baseName (RateBase b) noexcept
    {
        switch (b)
        {
            case RateBase::Whole:        return "1/1";
            case RateBase::Half:         return "1/2";
            case RateBase::Quarter:      return "1/4";
            case RateBase::Eighth:       return "1/8";
            case RateBase::Sixteenth:    return "1/16";
            case RateBase::ThirtySecond: return "1/32";
        }
        return "?";
    }
    static const char* modSuffix (RateMod m) noexcept
    {
        return m == RateMod::Dotted ? " D" : m == RateMod::Triplet ? " T" : "";
    }
};

} // namespace mosaic::engine
