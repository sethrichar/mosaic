#include "analysis/KeyDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace mosaic::analysis
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Krumhansl & Kessler (1982) probe-tone profiles. Published experimental data.
    const KeyProfiles kKK {
        { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f },
        { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.43f, 3.17f }
    };
    // Temperley (2001), "The Cognition of Basic Musical Structures". Published data.
    const KeyProfiles kTemperley {
        { 5.0f, 2.0f, 3.5f, 2.0f, 4.5f, 4.0f, 2.0f, 4.5f, 2.0f, 3.5f, 1.5f, 4.0f },
        { 5.0f, 2.0f, 3.5f, 4.5f, 2.0f, 4.0f, 2.0f, 4.5f, 3.5f, 2.0f, 1.5f, 4.0f }
    };

    // Iterative radix-2 complex FFT, in place. Plenty for an 8192-point analysis.
    void fft (std::vector<std::complex<float>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * kPi / (double) len;
            const std::complex<float> wlen ((float) std::cos (ang), (float) std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    float correlation (const float* x, const float* y, int n)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
        mx /= n; my /= n;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < n; ++i)
        {
            const double dx = x[i] - mx, dy = y[i] - my;
            sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
        }
        if (sxx <= 0.0 || syy <= 0.0) return 0.0f;
        return (float) (sxy / std::sqrt (sxx * syy));
    }
}

const KeyProfiles& KeyProfiles::krumhanslKessler() noexcept { return kKK; }
const KeyProfiles& KeyProfiles::temperley() noexcept        { return kTemperley; }

KeyResult KeyDetector::scoreChroma (const float chromaIn[12], const Settings& settings)
{
    KeyResult r;
    float sum = 0.0f;
    for (int i = 0; i < 12; ++i) sum += chromaIn[i];
    for (int i = 0; i < 12; ++i) r.chroma[i] = sum > 0.0f ? chromaIn[i] / sum : 0.0f;
    if (sum <= 0.0f)
        return r;

    const KeyProfiles& profiles = settings.profiles != nullptr ? *settings.profiles : KeyProfiles::temperley();
    float scores[24];
    for (int root = 0; root < 12; ++root)
    {
        float rotMaj[12], rotMin[12];
        for (int i = 0; i < 12; ++i)
        {
            rotMaj[(root + i) % 12] = profiles.major[i];
            rotMin[(root + i) % 12] = profiles.minor[i];
        }
        scores[root]      = correlation (r.chroma, rotMaj, 12);
        scores[12 + root] = correlation (r.chroma, rotMin, 12);
    }

    int best = 0;
    for (int i = 1; i < 24; ++i) if (scores[i] > scores[best]) best = i;
    const Key bestKey = Key::fromIndex (best);

    float rival = -1.0f;
    for (int i = 0; i < 24; ++i)
        if (Key::fromIndex (i).signaturePitchClass() != bestKey.signaturePitchClass())
            rival = std::max (rival, scores[i]);

    r.key = bestKey;
    r.bestScore = scores[best];
    r.margin = scores[best] - rival;
    const float strength = std::clamp (r.bestScore, 0.0f, 1.0f);
    const float marginTerm = std::clamp (r.margin / 0.2f, 0.0f, 1.0f);
    r.confidence = strength * (0.4f + 0.6f * marginTerm);
    r.level = r.confidence >= settings.highThreshold ? Confidence::High
            : r.confidence >= settings.lowThreshold  ? Confidence::Low : Confidence::None;
    r.found = r.level != Confidence::None;
    return r;
}

KeyResult KeyDetector::analyze (const float* left, const float* right, int64_t numSamples, double sampleRate,
                                const Settings& settings, std::atomic<bool>* cancel)
{
    KeyResult none;
    if (left == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return none;
    if (right == nullptr) right = left;

    // --- Downmix + lowpass + decimate to ~12 kHz ---
    const int factor = std::max (1, (int) std::lround (sampleRate / kTargetRate));
    const double rate = sampleRate / factor;
    const int64_t outLen = numSamples / factor;
    if (outLen < kFftSize / 4)
        return none;   // too short to say anything (< ~170 ms)

    std::vector<float> mono ((size_t) outLen);
    {
        // Windowed-sinc lowpass at 0.45 * rate, 63 taps, applied only at decimated positions.
        const int taps = 63, half = taps / 2;
        std::vector<float> h ((size_t) taps);
        const double fc = 0.45 / factor;   // cycles per input sample
        double hsum = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const int k = i - half;
            const double sinc = k == 0 ? 2.0 * fc : std::sin (2.0 * kPi * fc * k) / (kPi * k);
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (taps - 1));
            h[(size_t) i] = (float) (sinc * w);
            hsum += h[(size_t) i];
        }
        for (auto& v : h) v = (float) (v / hsum);

        for (int64_t o = 0; o < outLen; ++o)
        {
            if (cancel != nullptr && (o & 4095) == 0 && cancel->load()) return none;
            const int64_t centre = o * factor;
            double acc = 0.0;
            if (factor == 1)
                acc = 0.5 * (left[centre] + right[centre]);
            else
                for (int i = 0; i < taps; ++i)
                {
                    const int64_t p = centre + i - half;
                    if (p >= 0 && p < numSamples)
                        acc += h[(size_t) i] * 0.5 * (left[p] + right[p]);
                }
            mono[(size_t) o] = (float) acc;
        }
    }

    // --- STFT: collect spectral peaks as (midi note, weight) ---
    std::vector<float> window ((size_t) kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / (kFftSize - 1)));

    const int minBin = std::max (2, (int) std::floor (settings.minFreqHz * kFftSize / rate));
    const int maxBin = std::min (kFftSize / 2 - 2, (int) std::ceil (settings.maxFreqHz * kFftSize / rate));

    struct Peak { float midi; float weight; };
    std::vector<Peak> peaks;
    std::vector<std::complex<float>> frame ((size_t) kFftSize);
    double totalEnergy = 0.0, peakEnergy = 0.0;
    int frames = 0;

    for (int64_t start = 0; start + kFftSize / 2 <= outLen; start += kHop)
    {
        if (cancel != nullptr && cancel->load()) return none;

        double frameEnergy = 0.0;
        for (int i = 0; i < kFftSize; ++i)
        {
            const int64_t p = start + i;
            const float v = p < outLen ? mono[(size_t) p] * window[(size_t) i] : 0.0f;
            frame[(size_t) i] = { v, 0.0f };
            frameEnergy += (double) v * v;
        }
        if (frameEnergy < 1e-7)   // silence
            continue;
        ++frames;
        fft (frame);

        std::vector<float> mag ((size_t) kFftSize / 2 + 1);
        for (int b = 0; b <= kFftSize / 2; ++b) mag[(size_t) b] = std::abs (frame[(size_t) b]);

        double frameTotal = 0.0, framePeaks = 0.0;
        for (int b = minBin; b <= maxBin; ++b) frameTotal += (double) mag[(size_t) b] * mag[(size_t) b];

        for (int b = minBin; b <= maxBin; ++b)
        {
            const float m = mag[(size_t) b];
            if (m <= mag[(size_t) b - 1] || m < mag[(size_t) b + 1] || m <= 0.0f)
                continue;
            // Parabolic interpolation of the peak frequency.
            const float a = mag[(size_t) b - 1], c = mag[(size_t) b + 1];
            const float denom = a - 2.0f * m + c;
            const float delta = denom != 0.0f ? 0.5f * (a - c) / denom : 0.0f;
            const double freq = ((double) b + delta) * rate / kFftSize;
            const float midi = (float) (69.0 + 12.0 * std::log2 (freq / 440.0));
            float w = std::pow (m, settings.weightExponent);   // compress dynamics so loud notes don't dominate
            if (settings.bassEmphasis > 0.0f)
                w /= 1.0f + settings.bassEmphasis * std::max (0.0f, midi - 36.0f) / 36.0f;
            peaks.push_back ({ midi, w });
            framePeaks += (double) m * m;
        }
        totalEnergy += frameTotal;
        peakEnergy += framePeaks;
    }

    if (frames == 0 || peaks.empty())
        return none;

    // --- Tuning estimate: weighted histogram of fractional note offsets ---
    std::array<double, 24> tuningHist {};   // 24 bins over [-0.5, 0.5)
    for (const auto& p : peaks)
    {
        double frac = p.midi - std::floor (p.midi + 0.5);   // −0.5 … 0.5
        int bin = (int) std::floor ((frac + 0.5) * 24.0);
        bin = std::clamp (bin, 0, 23);
        tuningHist[(size_t) bin] += p.weight;
    }
    int bestTuning = 0;
    for (int i = 1; i < 24; ++i) if (tuningHist[(size_t) i] > tuningHist[(size_t) bestTuning]) bestTuning = i;
    const float tuningOffset = ((float) bestTuning + 0.5f) / 24.0f - 0.5f;   // semitones

    // --- Chroma ---
    float chroma[12] = {};
    auto credit = [&chroma] (float midi, float w)
    {
        const int note = (int) std::floor (midi + 0.5f);
        chroma[((note % 12) + 12) % 12] += w;
    };
    for (const auto& p : peaks)
    {
        const float midi = p.midi - tuningOffset;
        credit (midi, p.weight);
        if (settings.subharmonics > 0.0f)
            for (int h = 2; h <= 4; ++h)
                credit (midi - 12.0f * std::log2 ((float) h), p.weight * settings.subharmonics / (float) h);
        if (settings.fifthHarmonicCut > 0.0f)
            credit (midi - 12.0f * std::log2 (5.0f) + 4.0f + 0.14f, -p.weight * settings.fifthHarmonicCut);   // this peak as a 5th harmonic: its fundamental's major third... no: debit the peak's own class when it could be a 5th harmonic
    }
    for (auto& v : chroma) v = std::max (0.0f, v);

    KeyResult r = scoreChroma (chroma, settings);
    r.tuningCents = tuningOffset * 100.0f;
    r.tonalness = totalEnergy > 0.0 ? (float) (peakEnergy / totalEnergy) : 0.0f;

    if (r.tonalness < settings.minTonalness)
    {
        r.found = false;
        r.level = Confidence::None;
        r.confidence = std::min (r.confidence, settings.lowThreshold * 0.5f);
    }
    return r;
}

} // namespace mosaic::analysis
