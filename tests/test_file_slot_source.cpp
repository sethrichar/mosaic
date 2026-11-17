#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <thread>
#include <vector>

#include "sources/FileSlotSource.h"
#include "sources/SpscQueue.h"

using namespace mosaic::sources;

namespace
{
    std::unique_ptr<LoadedBuffer> makeBuffer (int64_t len, float value)
    {
        auto b = std::make_unique<LoadedBuffer>();
        b->left.assign ((size_t) len, value);
        b->right.assign ((size_t) len, -value);
        b->numSamples = len;
        b->sampleRate = 48000.0;
        return b;
    }
}

TEST_CASE ("spsc queue pushes and pops in order and reports full/empty", "[sources]")
{
    SpscQueue<int, 4> q;
    REQUIRE (q.empty());
    REQUIRE (q.push (1)); REQUIRE (q.push (2)); REQUIRE (q.push (3)); REQUIRE (q.push (4));
    REQUIRE_FALSE (q.push (5));
    int v = 0;
    REQUIRE (q.pop (v)); REQUIRE (v == 1);
    REQUIRE (q.push (5));
    REQUIRE (q.pop (v)); REQUIRE (v == 2);
    REQUIRE (q.pop (v)); REQUIRE (v == 3);
    REQUIRE (q.pop (v)); REQUIRE (v == 4);
    REQUIRE (q.pop (v)); REQUIRE (v == 5);
    REQUIRE_FALSE (q.pop (v));
    REQUIRE (q.empty());
}

TEST_CASE ("an empty file slot is silent and reports empty", "[sources]")
{
    FileSlotSource s;
    REQUIRE (s.isEmpty());
    REQUIRE (s.lengthInSamples() == 0);
    std::vector<float> l (64, 1.0f), r (64, 1.0f);
    s.read (0, l.data(), r.data(), 64);
    for (float v : l) REQUIRE (v == 0.0f);
}

TEST_CASE ("published buffers appear at the next poll and read with silence outside the file", "[sources]")
{
    FileSlotSource s;
    s.publish (makeBuffer (100, 0.5f));
    REQUIRE (s.isEmpty());        // not until the audio thread polls
    s.pollIncoming();
    REQUIRE_FALSE (s.isEmpty());
    REQUIRE (s.lengthInSamples() == 100);

    std::vector<float> l (120), r (120);
    s.read (-10, l.data(), r.data(), 120);   // 10 silent, 100 in range, 10 silent
    for (int i = 0; i < 120; ++i)
    {
        const bool in = i >= 10 && i < 110;
        REQUIRE (l[(size_t) i] == (in ? 0.5f : 0.0f));
        REQUIRE (r[(size_t) i] == (in ? -0.5f : 0.0f));
    }
    s.read (95, l.data(), r.data(), 10);
    for (int i = 0; i < 10; ++i) REQUIRE (l[(size_t) i] == (i < 5 ? 0.5f : 0.0f));
}

TEST_CASE ("replacing a buffer retires the old one and frees it on the message thread", "[sources]")
{
    FileSlotSource s;
    s.publish (makeBuffer (1000, 0.1f));
    s.pollIncoming();
    const size_t one = s.bytesInUse();
    REQUIRE (one >= 2000 * sizeof (float));

    s.publish (makeBuffer (2000, 0.2f));
    REQUIRE (s.bytesInUse() >= one + 4000 * sizeof (float));   // both alive until the swap
    s.pollIncoming();                                           // audio thread swaps, retires old
    s.collectGarbage();                                         // message thread frees it
    REQUIRE (s.bytesInUse() < one + 4000 * sizeof (float));
    REQUIRE (s.lengthInSamples() == 2000);

    s.publish (nullptr);                                        // clear
    s.pollIncoming();
    s.collectGarbage();
    REQUIRE (s.isEmpty());
    REQUIRE (s.bytesInUse() < 1024);
}

TEST_CASE ("many publishes between polls keep only the newest and leak nothing", "[sources]")
{
    FileSlotSource s;
    for (int i = 0; i < 50; ++i)
        s.publish (makeBuffer (10 + i, 0.01f * (float) i));   // more than the queue holds
    s.pollIncoming();                                           // takes the newest of the queued 8
    s.collectGarbage();                                         // frees them, publishes the pending newest
    s.pollIncoming();
    s.collectGarbage();
    REQUIRE (s.lengthInSamples() == 59);                        // the last one wins
    REQUIRE (s.bytesInUse() < 4 * 59 * sizeof (float) * 2 + 1024);
}

TEST_CASE ("concurrent publish and read never crash or read freed memory", "[sources][threads]")
{
    FileSlotSource s;
    std::atomic<bool> stop { false };

    std::thread audio ([&] {
        std::vector<float> l (256), r (256);
        int64_t pos = 0;
        while (! stop.load())
        {
            s.pollIncoming();
            s.read (pos, l.data(), r.data(), 256);
            for (int i = 0; i < 256; ++i)
                if (l[(size_t) i] != 0.0f) REQUIRE (l[(size_t) i] == -r[(size_t) i]);   // consistent pair => one buffer
            pos = (pos + 256) % 20000;
        }
    });

    for (int i = 0; i < 2000; ++i)
    {
        s.publish (makeBuffer (5000 + (i % 7) * 1000, 0.001f * (float) (i + 1)));
        s.collectGarbage();
    }
    stop = true;
    audio.join();
    s.pollIncoming();
    s.collectGarbage();
    REQUIRE_FALSE (s.isEmpty());
}
