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

double GridClock::quarterNotesPerBar (int numerator, int denominator) noexcept
{
    if (numerator <= 0 || denominator <= 0) return 4.0;
    return (double) numerator * 4.0 / (double) denominator;
}

int64_t GridClock::patternLengthSteps (double bars, double qnPerBar, double stepQN) noexcept
{
    if (bars <= 0.0 || stepQN <= 0.0) return 0;
    const int64_t steps = (int64_t) std::llround (bars * qnPerBar / stepQN);
    return steps < 1 ? 1 : steps;
}

int64_t GridClock::barsToSamples (double bars, double qnPerBar, double spq) noexcept
{
    return (int64_t) std::llround (bars * qnPerBar * spq);
}

HostBlock GridClock::advanced (const HostBlock& b, int n) noexcept
{
    HostBlock a = b;
    a.timeInSamples += n;
    a.ppqAtStart += (double) n / samplesPerQuarter (b);
    return a;
}

} // namespace mosaic::engine
