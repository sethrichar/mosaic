#pragma once

// Deterministic hashing for the stateless chooser and Free-mode slice picks (SPEC 4.3,
// 4.4, section 11). Never use anything else as a randomness source in the engine.

#include <cstdint>

namespace mosaic::engine
{

inline constexpr uint64_t mix64 (uint64_t x) noexcept
{
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

/// Hash of (seed, a, b, c). Order matters; every argument changes the result.
inline constexpr uint64_t hash (uint64_t seed, uint64_t a, uint64_t b = 0, uint64_t c = 0) noexcept
{
    constexpr uint64_t golden = 0x9e3779b97f4a7c15ULL;
    uint64_t h = mix64 (seed + golden);
    h = mix64 (h ^ (a + golden));
    h = mix64 (h ^ (b + golden * 2));
    h = mix64 (h ^ (c + golden * 3));
    return h;
}

/// Uniform double in [0, 1) from a hash.
inline constexpr double unitFloat (uint64_t h) noexcept
{
    return (double) (h >> 11) * (1.0 / 9007199254740992.0);
}

/// Floor division / modulo that behave for negative step indices.
inline constexpr int64_t floorDiv (int64_t a, int64_t b) noexcept
{
    const int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
inline constexpr int64_t floorMod (int64_t a, int64_t b) noexcept
{
    const int64_t m = a % b;
    return (m != 0 && ((m < 0) != (b < 0))) ? m + b : m;
}

} // namespace mosaic::engine
