#include "analysis/PitchShifter.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace mosaic::analysis
{

bool SignalsmithPitchShifter::shift (const float* inL, const float* inR, int64_t numSamples, double sampleRate,
                                     double semitones, float* outL, float* outR, std::atomic<bool>* cancel)
{
    if (inL == nullptr || outL == nullptr || outR == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return false;
    if (inR == nullptr) inR = inL;

    if (std::abs (semitones) < 1e-9)
    {
        std::memcpy (outL, inL, (size_t) numSamples * sizeof (float));
        std::memcpy (outR, inR, (size_t) numSamples * sizeof (float));
        return true;
    }

    signalsmith::stretch::SignalsmithStretch<float> stretch (kSeed);
    // Larger analysis blocks than the library default: measured pitch error stays within
    // ~2 cents across intervals (see the "experiment" test), at the cost of some transient
    // smearing. Scaled with the sample rate so the time constants stay the same.
    const int block    = std::max (1024, (int) std::lround (8192.0 * sampleRate / 48000.0));
    const int interval = std::max (128,  block / 8);
    stretch.configure (2, block, interval);
    // Tonality limit (per the library's guidance): keep harmonic structure below ~8 kHz.
    stretch.setTransposeSemitones ((float) semitones, (float) (8000.0 / sampleRate));

    const int inLatency  = stretch.inputLatency();
    const int outLatency = stretch.outputLatency();
    const int chunk = 4096;

    // Input stream: the file, then inputLatency zeros so the processing time reaches the
    // end. Output stream: outLatency samples of pre-roll, then the shifted file.
    const int64_t totalIn  = numSamples + inLatency;
    const int64_t totalOut = numSamples + outLatency;

    std::vector<float> bufInL ((size_t) chunk), bufInR ((size_t) chunk), bufOutL ((size_t) chunk), bufOutR ((size_t) chunk);
    float* inPtrs[2]  = { bufInL.data(),  bufInR.data() };
    float* outPtrs[2] = { bufOutL.data(), bufOutR.data() };

    // Seek so the processing time starts at the first input sample (see library README).
    {
        const int pre = std::min<int64_t> (inLatency, numSamples);
        std::vector<float> seekL ((size_t) inLatency, 0.0f), seekR ((size_t) inLatency, 0.0f);
        std::memcpy (seekL.data(), inL, (size_t) pre * sizeof (float));
        std::memcpy (seekR.data(), inR, (size_t) pre * sizeof (float));
        float* seekPtrs[2] = { seekL.data(), seekR.data() };
        stretch.seek (seekPtrs, inLatency, 1.0);
    }

    int64_t inPos = inLatency;    // already consumed by seek
    int64_t outPos = 0;
    while (outPos < totalOut)
    {
        if (cancel != nullptr && cancel->load()) return false;

        const int n = (int) std::min<int64_t> (chunk, totalOut - outPos);
        for (int i = 0; i < n; ++i)
        {
            const int64_t p = inPos + i;
            const bool in = p < numSamples;
            bufInL[(size_t) i] = in ? inL[p] : 0.0f;
            bufInR[(size_t) i] = in ? inR[p] : 0.0f;
        }
        const int inN = (int) std::min<int64_t> (n, std::max<int64_t> (0, totalIn - inPos));
        stretch.process (inPtrs, inN, outPtrs, n);
        inPos += inN;

        // Drop the pre-roll, then copy into the output.
        for (int i = 0; i < n; ++i)
        {
            const int64_t o = outPos + i - outLatency;
            if (o >= 0 && o < numSamples)
            {
                outL[o] = bufOutL[(size_t) i];
                outR[o] = bufOutR[(size_t) i];
            }
        }
        outPos += n;
    }
    return true;
}

} // namespace mosaic::analysis
