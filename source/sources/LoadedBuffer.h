#pragma once

// A fully loaded, session-rate, stereo float buffer. Created and destroyed on the
// message / loader threads; only ever *read* on the audio thread. Plain C++.

#include <cstdint>
#include <vector>

namespace mosaic::sources
{

struct LoadedBuffer
{
    std::vector<float> left, right;
    double  sampleRate   = 0.0;   ///< the session rate it was resampled to
    double  sourceRate   = 0.0;   ///< the file's own rate
    int64_t numSamples   = 0;

    size_t bytes() const noexcept { return (left.capacity() + right.capacity()) * sizeof (float); }
};

} // namespace mosaic::sources
