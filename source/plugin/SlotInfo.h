#pragma once

// Message-thread view of a slot's file: what the UI shows and what the state saves.
// The audio thread never touches this; it only sees the FileSlotSource.

#include <memory>
#include <vector>
#include <juce_core/juce_core.h>

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

    bool hasFile() const noexcept { return file != juce::File(); }
};

} // namespace mosaic
