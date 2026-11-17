#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs. Permanent once M8 freezes them. IDs starting with "probe" belong to the
// M1 routing spike and may be removed before the freeze.

namespace mosaic::params
{
    // --- M1: role and Send hub ---
    inline constexpr const char* role            = "role";            // 0 Mosaic, 1 Send
    inline constexpr const char* sendGroup       = "sendGroup";       // 0..3 = A..D
    inline constexpr const char* sendNumber      = "sendNumber";      // 0..7 = Send 1..8
    inline constexpr const char* sendMuteOutput  = "sendMuteOutput";  // Send role: mute the dry output
    inline constexpr const char* probeSource     = "probeSource";     // 0 none (engine), 1..8 Input N, 9..16 Send N
    inline constexpr const char* probeLookBehind = "probeLookBehind"; // index into kLookBehindSamples

    inline constexpr int kLookBehindSamples[] = { 0, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384 };
    inline constexpr int kNumLookBehindChoices = (int) (sizeof (kLookBehindSamples) / sizeof (int));
    inline constexpr int kDefaultLookBehindIndex = 6; // 2048

    inline constexpr int probeSourceNone  = 0;
    inline constexpr int probeSourceInput = 1;   // + bus index 0..7
    inline constexpr int probeSourceSend  = 9;   // + send index 0..7

    // --- M3: global mosaic controls (SPEC 4.7) ---
    inline constexpr const char* rate          = "rate";          // engine::Rate index 0..17
    inline constexpr const char* mode          = "mode";          // 0 Random, 1 Sequence
    inline constexpr const char* position      = "position";      // 0 Locked, 1 Free
    inline constexpr const char* fade          = "fade";          // ms 0..20
    inline constexpr const char* tileLength    = "tileLength";    // % 10..100
    inline constexpr const char* seed          = "seed";          // 0..kMaxSeed
    inline constexpr const char* patternLength = "patternLength"; // index into kPatternLengthBars
    inline constexpr const char* avoidRepeats  = "avoidRepeats";
    inline constexpr const char* outputGain    = "outputGain";    // dB

    inline constexpr int   kPatternLengthBars[] = { 0, 1, 2, 4, 8 };   // 0 = Off
    inline constexpr int   kNumPatternLengthChoices = 5;
    inline constexpr int   kMaxSeed  = 99999;
    inline constexpr float kMinGainDb = -60.0f;   // treated as -inf
    inline constexpr float kMaxGainDb = 12.0f;

    // --- M3: per-slot controls (SPEC 4.6), IDs "slot<1-8><Suffix>" ---
    inline constexpr const char* slotGainSuffix        = "Gain";        // dB
    inline constexpr const char* slotMuteSuffix        = "Mute";
    inline constexpr const char* slotWeightSuffix      = "Weight";      // 0..100
    inline constexpr const char* slotStartOffsetSuffix = "StartOffset"; // bars, -16..16
    inline constexpr const char* slotLoopSuffix        = "Loop";

    // --- M4: key (SPEC 7). Stored in state, not automatable: changes trigger a re-render. ---
    inline constexpr const char* keyMode  = "keyMode";    // 0 Auto, 1 Manual
    inline constexpr const char* keyRoot  = "keyRoot";    // 0..11
    inline constexpr const char* keyScale = "keyScale";   // 0 Major, 1 Minor
    inline constexpr const char* slotKeyModeSuffix   = "KeyMode";    // 0 Snap, 1 Original
    inline constexpr const char* slotDirectionSuffix = "Direction";  // 0 Nearest, 1 Up, 2 Down
    inline constexpr const char* slotTransposeSuffix = "Transpose";  // semitones -12..12
    inline constexpr const char* slotFineSuffix      = "Fine";       // cents -50..50

    inline juce::String slotParam (int slotIndex, const char* suffix)
    {
        return "slot" + juce::String (slotIndex + 1) + suffix;
    }

    /// dB parameter value to linear gain, with kMinGainDb meaning silence. Values within
    /// a thousandth of a dB of 0 are exactly 1.0: a parameter's default 0 dB comes back
    /// from normalisation as a few millionths of a dB, and 0 dB must be bit-exact
    /// pass-through (SPEC 7.6: a zero shift leaves the original untouched).
    inline float dbToGain (float db) noexcept
    {
        if (db <= kMinGainDb + 0.01f) return 0.0f;
        if (std::abs (db) < 0.001f)   return 1.0f;
        return juce::Decibels::decibelsToGain (db);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
