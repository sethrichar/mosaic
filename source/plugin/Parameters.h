#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs. Permanent once M8 freezes them. IDs starting with "probe" belong to the
// M1 routing spike and may be removed before the freeze.

namespace mosaic::params
{
    inline constexpr const char* role            = "role";            // 0 Mosaic, 1 Send
    inline constexpr const char* sendGroup       = "sendGroup";       // 0..3 = A..D
    inline constexpr const char* sendNumber      = "sendNumber";      // 0..7 = Send 1..8
    inline constexpr const char* sendMuteOutput  = "sendMuteOutput";  // Send role: mute the dry output
    inline constexpr const char* probeSource     = "probeSource";     // 0 none, 1..8 Input N, 9..16 Send N
    inline constexpr const char* probeLookBehind = "probeLookBehind"; // index into kLookBehindSamples

    inline constexpr int kLookBehindSamples[] = { 0, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384 };
    inline constexpr int kNumLookBehindChoices = (int) (sizeof (kLookBehindSamples) / sizeof (int));
    inline constexpr int kDefaultLookBehindIndex = 6; // 2048

    inline constexpr int probeSourceNone  = 0;
    inline constexpr int probeSourceInput = 1;   // + bus index 0..7
    inline constexpr int probeSourceSend  = 9;   // + send index 0..7

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
