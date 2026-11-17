#include <catch2/catch_test_macros.hpp>

#include "analysis/KeySnap.h"

using namespace mosaic::analysis;

TEST_CASE ("key names, indices and signatures", "[key]")
{
    REQUIRE (Key { 9, Scale::Minor }.name() == "A min");
    REQUIRE (Key { 0, Scale::Major }.name() == "C maj");
    REQUIRE (Key { 9, Scale::Minor }.signaturePitchClass() == 0);     // A minor ~ C major
    REQUIRE (Key { 6, Scale::Minor }.signaturePitchClass() == 9);     // F# minor ~ A major
    for (int i = 0; i < 24; ++i) REQUIRE (Key::fromIndex (i).toIndex() == i);
}

TEST_CASE ("spec examples: target A minor", "[key][snap]")
{
    const Key target { 9, Scale::Minor };
    REQUIRE (KeySnap::shiftSemitones ({ 0, Scale::Major }, target, Direction::Nearest) == 0);    // C major
    REQUIRE (KeySnap::shiftSemitones ({ 2, Scale::Minor }, target, Direction::Nearest) == -5);   // D minor
    REQUIRE (KeySnap::shiftSemitones ({ 4, Scale::Major }, target, Direction::Nearest) == -4);   // E major
    REQUIRE (KeySnap::shiftSemitones ({ 0, Scale::Minor }, target, Direction::Nearest) == -3);   // C minor -> Eb sig -> C
}

TEST_CASE ("all 24 x 24 key pairs: ranges, relatives, tritone and directions", "[key][snap]")
{
    for (int a = 0; a < 24; ++a)
        for (int b = 0; b < 24; ++b)
        {
            const Key src = Key::fromIndex (a), dst = Key::fromIndex (b);
            const int near = KeySnap::shiftSemitones (src, dst, Direction::Nearest);
            const int up   = KeySnap::shiftSemitones (src, dst, Direction::Up);
            const int down = KeySnap::shiftSemitones (src, dst, Direction::Down);

            REQUIRE (near >= -6); REQUIRE (near <= 5);
            REQUIRE (up >= 0);    REQUIRE (up <= 11);
            REQUIRE (down >= -11); REQUIRE (down <= 0);

            // All three land on the target signature.
            const int sig = ((src.signaturePitchClass() + near) % 12 + 12) % 12;
            REQUIRE (sig == dst.signaturePitchClass());
            REQUIRE (((src.signaturePitchClass() + up) % 12 + 12) % 12 == dst.signaturePitchClass());
            REQUIRE (((src.signaturePitchClass() + down) % 12 + 12) % 12 == dst.signaturePitchClass());

            if (src.signaturePitchClass() == dst.signaturePitchClass())
            {
                REQUIRE (near == 0); REQUIRE (up == 0); REQUIRE (down == 0);   // relative keys: in key already
            }
            else
            {
                REQUIRE (up - down == 12);
                REQUIRE ((near == up || near == down));
            }
            if (((dst.signaturePitchClass() - src.signaturePitchClass()) % 12 + 12) % 12 == 6)
                REQUIRE (near == -6);                                            // tritone resolves downward
        }
}

TEST_CASE ("total shift adds transpose and fine", "[key][snap]")
{
    const Key c { 0, Scale::Major }, d { 2, Scale::Major };
    REQUIRE (KeySnap::totalShift (c, d, Direction::Nearest, 12, 50.0) == 14.5);
    REQUIRE (KeySnap::totalShift (c, d, Direction::Down, 0, -25.0) == -10.25);
    REQUIRE (KeySnap::manualShift (-3, 10.0) == -2.9);
}

TEST_CASE ("key names parse from sample-pack style strings", "[key][parse]")
{
    Key k;
    auto check = [&] (const char* s, int root, Scale scale) {
        INFO (s);
        REQUIRE (Key::parse (s, k));
        REQUIRE (k.root == root);
        REQUIRE (k.scale == scale);
    };
    check ("Am", 9, Scale::Minor);
    check ("Amin", 9, Scale::Minor);
    check ("A minor", 9, Scale::Minor);
    check ("A min", 9, Scale::Minor);
    check ("Cmaj", 0, Scale::Major);
    check ("C major", 0, Scale::Major);
    check ("C", 0, Scale::Major);
    check ("F#", 6, Scale::Major);
    check ("F#m", 6, Scale::Minor);
    check ("F#min", 6, Scale::Minor);
    check ("Gb", 6, Scale::Major);
    check ("Bbm", 10, Scale::Minor);
    check ("Bb minor", 10, Scale::Minor);
    check ("Ebmaj", 3, Scale::Major);
    check ("B", 11, Scale::Major);
    check ("Bm", 11, Scale::Minor);
    check ("bmin", 11, Scale::Minor);
    check ("Bbmaj", 10, Scale::Major);
    check ("Db", 1, Scale::Major);
    check ("g_sharp_min", 8, Scale::Minor);
    REQUIRE_FALSE (Key::parse ("drums", k));
    REQUIRE_FALSE (Key::parse ("H", k));
    REQUIRE_FALSE (Key::parse ("", k));
    REQUIRE_FALSE (Key::parse ("Cx", k));
}
