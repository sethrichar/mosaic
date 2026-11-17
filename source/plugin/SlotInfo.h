#pragma once

// Message-thread view of a slot's file: what the UI shows and what the state saves.
// The audio thread never touches this; it only sees the FileSlotSource.

#include <memory>
#include <vector>
#include <juce_core/juce_core.h>

#include "analysis/KeyDetector.h"

namespace mosaic
{

struct SlotInfo
{
    juce::File   file;                 ///< empty = no file
    bool         missing = false;      ///< path saved in the session but not found on disk
    bool         loading = false;
    juce::String error;                ///< last load error, if any

    int64_t      lengthSamples     = 0;    ///< at session rate
    double       sourceSampleRate  = 0.0;
    int          sourceChannels    = 0;
    size_t       bytes             = 0;    ///< bytes of the loaded buffer
    int          generation        = 0;    ///< bumps on every load request; stale results are dropped

    /// 2 * kOverviewBins floats: (min, max) pairs of the mono mix, for the waveform view.
    std::shared_ptr<const std::vector<float>> overview;

    // --- M4 key (SPEC 7) ---
    bool                keyDetected = false;     ///< detection has run for this file
    analysis::KeyResult key;                     ///< detector output (key.found may be false)
    bool                hasOverride = false;     ///< user-set source key
    analysis::Key       overrideKey;
    bool                analyzing = false;       ///< load + detect in progress
    bool                rendering = false;       ///< pitch render in progress
    double              renderedShift = 0.0;     ///< semitones baked into the published buffer
    double              desiredShift  = 0.0;     ///< what the current settings ask for

    bool hasFile() const noexcept { return file != juce::File(); }

    /// The key Snap works from: the override, else a detection that found a key.
    bool sourceKeyKnown() const noexcept { return hasOverride || (keyDetected && key.found); }
    analysis::Key sourceKey() const noexcept { return hasOverride ? overrideKey : key.key; }
    /// True when this slot may set the global Key in Auto mode (SPEC 7.2).
    bool confidentSourceKey() const noexcept
    {
        return hasOverride || (keyDetected && key.found && key.level == analysis::Confidence::High);
    }
};

} // namespace mosaic
