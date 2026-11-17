#pragma once

// In-house key detection (SPEC 7.5). Plain C++, no third-party code.
//
// Mono downmix -> lowpass + decimate to ~12 kHz -> Hann STFT (8192) -> spectral peaks
// -> tuning estimate -> 12-bin pitch-class profile -> correlation with 24 rotated key
// profiles. Confidence comes from the best match's strength and its margin over the
// best candidate with a *different* signature (relative keys don't count as rivals).
// Never run this on the audio thread.

#include <atomic>
#include <cstdint>

#include "analysis/Key.h"

namespace mosaic::analysis
{

enum class Confidence : int { None = 0, Low = 1, High = 2 };

struct KeyResult
{
    bool       found = false;        ///< false: no clear key (drums, noise, silence)
    Key        key;
    float      confidence = 0.0f;    ///< 0..1
    Confidence level = Confidence::None;
    float      tuningCents = 0.0f;   ///< estimated deviation from A440, −50..50
    float      chroma[12] = {};      ///< normalised pitch-class profile (sums to 1)
    float      bestScore = 0.0f;     ///< correlation of the winning key
    float      margin = 0.0f;        ///< bestScore − best non-relative rival
    float      tonalness = 0.0f;     ///< 0..1, how peaky the spectrum is
};

/// Swappable key profile table (SPEC 7.5: "keep the profile table swappable").
struct KeyProfiles
{
    float major[12];
    float minor[12];

    static const KeyProfiles& krumhanslKessler() noexcept;
    static const KeyProfiles& temperley() noexcept;
};

struct KeyDetectorSettings
{
    const KeyProfiles* profiles = nullptr;   ///< nullptr = Temperley (chosen by experiment, see tests)
    float highThreshold = 0.55f;   ///< confidence at or above: High
    float lowThreshold  = 0.20f;   ///< confidence at or above: Low; below: None
    float minTonalness  = 0.12f;   ///< below this the material is treated as unpitched
    double minFreqHz = 55.0, maxFreqHz = 2200.0;

    // Chroma weighting knobs (tuned against synthetic material and Seth's set).
    float weightExponent = 0.5f;      ///< peak weight = magnitude ^ exponent (0.5 = sqrt)
    float bassEmphasis   = 1.0f;      ///< > 0 favours low notes: weight /= 1 + e * max(0, midi - 36) / 36
    float subharmonics   = 0.0f;      ///< > 0 also credits f/2, f/3, f/4 with weight * s / h
    float fifthHarmonicCut = 0.0f;    ///< > 0 debits the major third of each peak's "fundamental" (5th harmonic)
};

class KeyDetector
{
public:
    using Settings = KeyDetectorSettings;

    /// Analyses interleaved-free stereo (right may equal left, or be null for mono).
    /// `cancel` may be polled; a cancelled analysis returns found == false.
    static KeyResult analyze (const float* left, const float* right, int64_t numSamples, double sampleRate,
                              const Settings& settings = {}, std::atomic<bool>* cancel = nullptr);

    /// Scores a chroma vector alone (exposed for tests and the tool).
    static KeyResult scoreChroma (const float chroma[12], const Settings& settings = {});

    static constexpr int kFftSize = 8192;
    static constexpr int kHop = 2048;
    static constexpr double kTargetRate = 12000.0;
};

} // namespace mosaic::analysis
