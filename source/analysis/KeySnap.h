#pragma once

// Snap-to-key math (SPEC 7.4): aligns key *signatures*, so relative major/minor pairs
// count as already in key. Pure functions, plain C++.

#include "analysis/Key.h"

namespace mosaic::analysis
{

enum class Direction : int { Nearest = 0, Up = 1, Down = 2 };

class KeySnap
{
public:
    /// Semitones to transpose `source` material so it sits in `target`'s signature.
    /// Nearest: −6 … +5 (a tritone resolves downward). Up: 0 … +11. Down: −11 … 0.
    static int shiftSemitones (Key source, Key target, Direction direction) noexcept;

    /// Total pitch change in semitones: snap shift + Transpose + Fine (cents).
    static double totalShift (Key source, Key target, Direction direction, int transposeSemitones, double fineCents) noexcept
    {
        return (double) shiftSemitones (source, target, direction) + (double) transposeSemitones + fineCents / 100.0;
    }

    /// Transpose + Fine only (Original mode, or no confident source key).
    static double manualShift (int transposeSemitones, double fineCents) noexcept
    {
        return (double) transposeSemitones + fineCents / 100.0;
    }
};

} // namespace mosaic::analysis
