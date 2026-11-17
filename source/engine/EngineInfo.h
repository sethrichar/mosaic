#pragma once

// Plain C++ — no JUCE here. The engine must compile and test on its own
// (CLAUDE.md, "Engine boundaries").

namespace mosaic::engine
{
    /// Number of source slots in a Mosaic instance (SPEC section 2).
    inline constexpr int kNumSlots = 8;

    /// Version of the engine library, for tests and the UI "about" readout.
    const char* versionString() noexcept;
}
