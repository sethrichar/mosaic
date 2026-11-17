#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "hub/SendHub.h"

using namespace mosaic::hub;

namespace
{
    std::vector<float> ramp (int64_t pos, int n, float scale = 1.0f)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (pos + i) * scale;
        return v;
    }
}

TEST_CASE ("unallocated channel reads as silence and refuses writes", "[hub]")
{
    SendChannel ch;
    REQUIRE_FALSE (ch.isAllocated());

    std::vector<float> l (64, 1.0f), r (64, 1.0f);
    auto in = ramp (0, 64);
    REQUIRE_FALSE (ch.write (1, 0, in.data(), in.data(), 64));

    auto res = ch.read (0, l.data(), r.data(), 64);
    REQUIRE_FALSE (res.valid);
    REQUIRE (res.head == kNoHead);
    for (auto s : l) REQUIRE (s == 0.0f);
    for (auto s : r) REQUIRE (s == 0.0f);
    REQUIRE (ch.peakOfLatest (64) == 0.0f);
}

TEST_CASE ("write then read at the same timeline position is sample-exact", "[hub]")
{
    SendChannel ch;
    ch.allocate();
    REQUIRE (ch.isAllocated());

    const int64_t T = 1000;
    auto inL = ramp (T, 256), inR = ramp (T, 256, -1.0f);
    REQUIRE (ch.write (7, T, inL.data(), inR.data(), 256));
    REQUIRE (ch.head() == T + 256);
    REQUIRE (ch.owner() == 7);

    std::vector<float> l (256), r (256);
    auto res = ch.read (T, l.data(), r.data(), 256);
    REQUIRE (res.valid);
    REQUIRE (l == inL);
    REQUIRE (r == inR);

    // A sub-range inside the written block is also valid.
    std::vector<float> l2 (100), r2 (100);
    REQUIRE (ch.read (T + 50, l2.data(), r2.data(), 100).valid);
    REQUIRE (l2 == ramp (T + 50, 100));
}

TEST_CASE ("reading past the head or behind the ring gives silence", "[hub]")
{
    SendChannel ch;
    ch.allocate();
    auto in = ramp (0, 512);
    ch.write (1, 0, in.data(), in.data(), 512);

    std::vector<float> l (512, 9.0f), r (512, 9.0f);

    SECTION ("one sample past the head")
    {
        auto res = ch.read (1, l.data(), r.data(), 512);
        REQUIRE_FALSE (res.valid);
        for (auto s : l) REQUIRE (s == 0.0f);
    }
    SECTION ("exactly up to the head is fine")
    {
        REQUIRE (ch.read (0, l.data(), r.data(), 512).valid);
    }
    SECTION ("further back than the ring holds")
    {
        // Write contiguously until the ring is a little more than full.
        std::vector<float> chunk ((size_t) kRingSize / 4, 0.5f);
        int64_t pos = 512;
        for (int i = 0; i < 4; ++i, pos += kRingSize / 4)
            ch.write (1, pos, chunk.data(), chunk.data(), kRingSize / 4);
        REQUIRE (ch.head() == 512 + kRingSize);
        REQUIRE (ch.tail() == 512);                                         // first 512 fell off
        REQUIRE_FALSE (ch.read (0, l.data(), r.data(), 64).valid);
        REQUIRE (ch.read (512, l.data(), r.data(), 64).valid);              // oldest still valid
    }
}

TEST_CASE ("never-written positions behind the head are a miss, not zeros", "[hub]")
{
    SendChannel ch;
    ch.allocate();
    auto in = ramp (1000, 256);
    ch.write (1, 1000, in.data(), in.data(), 256);
    REQUIRE (ch.tail() == 1000);

    std::vector<float> l (256), r (256);
    REQUIRE_FALSE (ch.read (744, l.data(), r.data(), 256).valid);    // entirely before tail
    REQUIRE_FALSE (ch.read (900, l.data(), r.data(), 256).valid);    // straddles tail
    REQUIRE (ch.read (1000, l.data(), r.data(), 256).valid);
}

TEST_CASE ("a locate or loop wrap invalidates audio from before the jump", "[hub]")
{
    SendChannel ch;
    ch.allocate();
    auto a = ramp (0, 512);
    ch.write (1, 0, a.data(), a.data(), 512);
    auto b = ramp (512, 512);
    ch.write (1, 512, b.data(), b.data(), 512);        // contiguous: tail stays 0
    REQUIRE (ch.tail() == 0);

    std::vector<float> l (256), r (256);
    REQUIRE (ch.read (0, l.data(), r.data(), 256).valid);

    auto c = ramp (100000, 256);
    ch.write (1, 100000, c.data(), c.data(), 256);     // locate: tail jumps
    REQUIRE (ch.tail() == 100000);
    REQUIRE (ch.head() == 100256);
    REQUIRE_FALSE (ch.read (0, l.data(), r.data(), 256).valid);       // old audio is gone
    REQUIRE_FALSE (ch.read (99900, l.data(), r.data(), 256).valid);
    REQUIRE (ch.read (100000, l.data(), r.data(), 256).valid);
    REQUIRE (l == c);
    REQUIRE (ch.peakOfLatest (100000) == c.back());                   // clamped to the valid region
}

TEST_CASE ("writes wrap around the ring boundary correctly", "[hub]")
{
    SendChannel ch;
    ch.allocate();
    const int64_t T = kRingSize - 100;         // block straddles the wrap point
    auto in = ramp (T, 300);
    REQUIRE (ch.write (1, T, in.data(), in.data(), 300));

    std::vector<float> l (300), r (300);
    REQUIRE (ch.read (T, l.data(), r.data(), 300).valid);
    REQUIRE (l == in);
}

TEST_CASE ("negative (pre-roll) timeline positions work", "[hub]")
{
    SendChannel ch;
    ch.allocate();
    auto in = ramp (-200, 400);
    REQUIRE (ch.write (1, -200, in.data(), in.data(), 400));
    std::vector<float> l (400), r (400);
    REQUIRE (ch.read (-200, l.data(), r.data(), 400).valid);
    REQUIRE (l == in);
}

TEST_CASE ("look-behind makes the read independent of processing order", "[hub]")
{
    // Consumer at timeline T asks for T - L. With L >= block size the data is always
    // there whether the producer ran before or after the consumer this block.
    SendChannel ch;
    ch.allocate();
    const int n = 256, L = 256;
    std::vector<float> l (n), r (n);

    int64_t T = 0;
    // Block 0: consumer first (nothing written yet) -> invalid, expected during warm-up.
    REQUIRE_FALSE (ch.read (T - L, l.data(), r.data(), n).valid);
    { auto in = ramp (T, n); ch.write (1, T, in.data(), in.data(), n); }

    for (int block = 1; block < 50; ++block)
    {
        T += n;
        // consumer BEFORE producer this block
        auto res = ch.read (T - L, l.data(), r.data(), n);
        REQUIRE (res.valid);
        REQUIRE (l == ramp (T - L, n));
        REQUIRE (res.head - T == 0);              // skew: producer exactly one block behind
        auto in = ramp (T, n);
        ch.write (1, T, in.data(), in.data(), n);
    }
}

TEST_CASE ("a second producer on the same channel is counted as a collision", "[hub]")
{
    SendChannel ch;
    ch.allocate();
    auto in = ramp (0, 64);
    ch.write (1, 0, in.data(), in.data(), 64);
    REQUIRE (ch.collisionCount() == 0);
    ch.write (1, 64, in.data(), in.data(), 64);
    REQUIRE (ch.collisionCount() == 0);
    ch.write (2, 128, in.data(), in.data(), 64);
    REQUIRE (ch.collisionCount() == 1);
    ch.write (1, 192, in.data(), in.data(), 64);
    REQUIRE (ch.collisionCount() == 2);

    ch.releaseOwnership (3);                       // not the owner: no effect
    REQUIRE (ch.owner() == 1);
    ch.releaseOwnership (1);
    REQUIRE (ch.owner() == 0);
    ch.write (2, 256, in.data(), in.data(), 64);   // fresh claim, no collision
    REQUIRE (ch.collisionCount() == 2);
}

TEST_CASE ("peakOfLatest reports the newest samples", "[hub]")
{
    SendChannel ch;
    ch.allocate();
    std::vector<float> quiet (100, 0.1f), loud (100, 0.0f);
    loud[99] = -0.9f;
    ch.write (1, 0, quiet.data(), quiet.data(), 100);
    ch.write (1, 100, loud.data(), loud.data(), 100);
    REQUIRE (ch.peakOfLatest (100) == 0.9f);
    REQUIRE (ch.peakOfLatest (1) == 0.9f);
    ch.write (1, 200, quiet.data(), quiet.data(), 100);
    REQUIRE (ch.peakOfLatest (100) == 0.1f);
}

TEST_CASE ("hub singleton hands out unique ids and distinct channels", "[hub]")
{
    auto& hub = SendHub::instance();
    REQUIRE (&hub == &SendHub::instance());
    const int a = hub.newInstanceId(), b = hub.newInstanceId();
    REQUIRE (a != 0);
    REQUIRE (a != b);
    REQUIRE (&hub.channel (0, 0) != &hub.channel (0, 1));
    REQUIRE (&hub.channel (1, 0) == &hub.channelAt (SendHub::index (1, 0)));
}
