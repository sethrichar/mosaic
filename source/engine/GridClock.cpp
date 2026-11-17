#include "engine/GridClock.h"

#include <cmath>

namespace mosaic::engine
{

int64_t GridClock::boundaryOffset (const HostBlock& b, double stepQN, int64_t k) noexcept
{
    const double spq = samplesPerQuarter (b);
    const double x = ((double) k * stepQN - b.ppqAtStart) * spq - kSnapEpsilonSamples;
    return (int64_t) std::ceil (x);
}

int64_t GridClock::stepAtStart (const HostBlock& b, double stepQN) noexcept
{
    const double spq = samplesPerQuarter (b);
    int64_t k = (int64_t) std::floor (b.ppqAtStart / stepQN + kSnapEpsilonSamples / (stepQN * spq));

    // Make k consistent with boundaryOffset, whatever floating point did.
    for (int guard = 0; guard < 4 && boundaryOffset (b, stepQN, k) > 0; ++guard)      --k;
    for (int guard = 0; guard < 4 && boundaryOffset (b, stepQN, k + 1) <= 0; ++guard) ++k;
    return k;
}

int64_t GridClock::stepLengthSamples (const HostBlock& b, double stepQN, int64_t k) noexcept
{
    const int64_t len = boundaryOffset (b, stepQN, k + 1) - boundaryOffset (b, stepQN, k);
    return len < 1 ? 1 : len;
}

HostBlock GridClock::advanced (const HostBlock& b, int n) noexcept
{
    HostBlock a = b;
    a.timeInSamples += n;
    a.ppqAtStart += (double) n / samplesPerQuarter (b);
    return a;
}

} // namespace mosaic::engine
