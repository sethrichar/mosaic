#include <catch2/catch_test_macros.hpp>

#include "engine/SlicePosition.h"
#include "TestSources.h"

using namespace mosaic::engine;
using mosaic::test::RampSource;
using mosaic::test::LiveTestSource;

TEST_CASE ("locked position follows the timeline with offset and loop", "[slice]")
{
    RampSource file (1000, 0.1f);
    SlotPlacement place; place.loop = true; place.startOffsetSamples = 0;

    auto r = SlicePosition::locked (file, place, 2500, 64);
    REQUIRE_FALSE (r.silent);
    REQUIRE (r.sourcePos == 500);
    REQUIRE (r.length == 64);

    r = SlicePosition::locked (file, place, 990, 64);     // run stops at the loop wrap
    REQUIRE (r.sourcePos == 990);
    REQUIRE (r.length == 10);

    place.startOffsetSamples = 300;
    r = SlicePosition::locked (file, place, 100, 64);     // 100 - 300 = -200 -> 800
    REQUIRE (r.sourcePos == 800);

    SECTION ("loop off: silence before the start and past the end")
    {
        place.loop = false; place.startOffsetSamples = 0;
        r = SlicePosition::locked (file, place, -50, 64);
        REQUIRE (r.silent); REQUIRE (r.length == 50);           // silence until the file starts
        r = SlicePosition::locked (file, place, 1000, 64);
        REQUIRE (r.silent); REQUIRE (r.length == 64);
        r = SlicePosition::locked (file, place, 980, 64);
        REQUIRE_FALSE (r.silent); REQUIRE (r.length == 20);
    }
}

TEST_CASE ("file shorter than a step still maps correctly", "[slice]")
{
    RampSource file (100, 0.1f);
    SlotPlacement place;
    auto r = SlicePosition::locked (file, place, 12345, 512);
    REQUIRE (r.sourcePos == 12345 % 100);
    REQUIRE (r.length == 100 - 12345 % 100);
}

TEST_CASE ("live sources read now in both modes", "[slice]")
{
    LiveTestSource live;
    SlotPlacement place; place.startOffsetSamples = 999;
    auto r = SlicePosition::locked (live, place, 777, 32);
    REQUIRE (r.sourcePos == 777); REQUIRE_FALSE (r.silent); REQUIRE (r.length == 32);
    r = SlicePosition::free (live, place, 777, 700, 3, 1, 42, 512, 32);
    REQUIRE (r.sourcePos == 777);
}

TEST_CASE ("free position picks a slice from (seed, step, slot) and reads it contiguously", "[slice]")
{
    RampSource file (48000, 0.1f);
    SlotPlacement place;
    const int64_t stepLen = 6000;   // 8 slices
    const int64_t slice = SlicePosition::freeSliceIndex (48000, stepLen, 10, 2, 7);
    REQUIRE (slice >= 0); REQUIRE (slice < 8);
    REQUIRE (slice == SlicePosition::freeSliceIndex (48000, stepLen, 10, 2, 7));
    const bool notStuck = SlicePosition::freeSliceIndex (48000, stepLen, 11, 2, 7) != slice
                       || SlicePosition::freeSliceIndex (48000, stepLen, 12, 2, 7) != slice;
    REQUIRE (notStuck);

    const int64_t tileStart = 100000;
    auto r = SlicePosition::free (file, place, tileStart + 1234, tileStart, 10, 2, 7, stepLen, 256);
    REQUIRE_FALSE (r.silent);
    REQUIRE (r.sourcePos == slice * stepLen + 1234);
    REQUIRE (r.length == 256);

    // The crossfade tail of a tile runs past its slice end; it keeps reading the file
    // (and goes silent at the file end, never wrapping).
    r = SlicePosition::free (file, place, tileStart + stepLen + 10, tileStart, 10, 2, 7, stepLen, 256);
    REQUIRE ((r.sourcePos == slice * stepLen + stepLen + 10 || r.silent));

    // File shorter than a step: one slice, silence past the end.
    RampSource shortFile (1000, 0.1f);
    r = SlicePosition::free (shortFile, place, tileStart + 1500, tileStart, 10, 2, 7, stepLen, 256);
    REQUIRE (r.silent);
    r = SlicePosition::free (shortFile, place, tileStart + 900, tileStart, 10, 2, 7, stepLen, 256);
    REQUIRE_FALSE (r.silent); REQUIRE (r.sourcePos == 900); REQUIRE (r.length == 100);
}
