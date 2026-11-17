#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <vector>

#include "analysis/PitchShifter.h"

using namespace mosaic::analysis;

namespace
{
    constexpr double kSr = 48000.0;

    // Dominant frequency of a segment via a long FFT with parabolic interpolation.
    double dominantFrequency (const std::vector<float>& x, size_t start, size_t n)
    {
        std::vector<std::complex<double>> a (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) (n - 1));
            a[i] = { (double) x[start + i] * w, 0.0 };
        }
        // naive DFT around the expected range is too slow; use a radix-2 FFT
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * M_PI / (double) len;
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
                }
            }
        }
        size_t best = 1;
        for (size_t b = 2; b < n / 2; ++b) if (std::abs (a[b]) > std::abs (a[best])) best = b;
        const double m0 = std::abs (a[best - 1]), m1 = std::abs (a[best]), m2 = std::abs (a[best + 1]);
        const double denom = m0 - 2.0 * m1 + m2;
        const double delta = denom != 0.0 ? 0.5 * (m0 - m2) / denom : 0.0;
        return ((double) best + delta) * kSr / (double) n;
    }
}

TEST_CASE ("a 440 Hz sine shifted +3 semitones measures 523.25 Hz within 3 cents", "[shifter]")
{
    const int64_t n = (int64_t) kSr * 3;
    std::vector<float> in ((size_t) n), outL ((size_t) n), outR ((size_t) n);
    for (int64_t i = 0; i < n; ++i) in[(size_t) i] = 0.5f * (float) std::sin (2.0 * M_PI * 440.0 * (double) i / kSr);

    SignalsmithPitchShifter shifter;
    REQUIRE (shifter.shift (in.data(), in.data(), n, kSr, 3.0, outL.data(), outR.data()));

    const double f = dominantFrequency (outL, (size_t) kSr, 65536);
    const double cents = 1200.0 * std::log2 (f / 523.2511);
    INFO ("measured " << f << " Hz, " << cents << " cents");
    REQUIRE (std::fabs (cents) < 3.0);
    float maxDiff = 0.0f;   // identical inputs: the two channels must agree closely (not bit-exactly: the library processes them jointly)
    for (size_t i = 0; i < outL.size(); ++i) maxDiff = std::max (maxDiff, std::fabs (outL[i] - outR[i]));
    REQUIRE (maxDiff < 1.0e-3f);

    float peak = 0.0f;
    for (size_t i = (size_t) kSr; i < (size_t) (2 * kSr); ++i) peak = std::max (peak, std::fabs (outL[i]));
    REQUIRE (peak > 0.3f); REQUIRE (peak < 0.7f);
}

TEST_CASE ("a click track shifted +3 semitones keeps every click within 1 ms, same length", "[shifter]")
{
    const int64_t n = (int64_t) kSr * 4;
    std::vector<float> in ((size_t) n, 0.0f), outL ((size_t) n), outR ((size_t) n);
    std::vector<int64_t> clicks;
    for (int64_t t = 24000; t < n - 24000; t += 24000)   // every 0.5 s, none at the very edges
    {
        clicks.push_back (t);
        for (int i = 0; i < 48; ++i) in[(size_t) (t + i)] = 0.9f * (float) std::exp (-i / 8.0) * (i % 2 ? -1.0f : 1.0f);
    }

    SignalsmithPitchShifter shifter;
    REQUIRE (shifter.shift (in.data(), in.data(), n, kSr, 3.0, outL.data(), outR.data()));
    REQUIRE ((int64_t) outL.size() == n);

    const int64_t window = 480;   // 10 ms search either side; expect < 1 ms (48 samples)
    for (int64_t c : clicks)
    {
        int64_t best = c; float bestV = 0.0f;
        for (int64_t t = c - window; t <= c + window; ++t)
            if (std::fabs (outL[(size_t) t]) > bestV) { bestV = std::fabs (outL[(size_t) t]); best = t; }
        INFO ("click at " << c << " found at " << best << " (" << (best - c) << " samples), amp " << bestV);
        REQUIRE (bestV > 0.05f);
        REQUIRE (std::llabs (best - c) <= 48);
    }
}

TEST_CASE ("shift of 0 is bit-identical and rendering is deterministic", "[shifter]")
{
    const int64_t n = 20000;
    std::vector<float> in ((size_t) n), r ((size_t) n), a ((size_t) n), b ((size_t) n), c ((size_t) n), d ((size_t) n);
    for (int64_t i = 0; i < n; ++i) { in[(size_t) i] = (float) std::sin (0.01 * (double) i * (1.0 + i * 1e-5)); r[(size_t) i] = -in[(size_t) i]; }

    SignalsmithPitchShifter shifter;
    REQUIRE (shifter.shift (in.data(), r.data(), n, kSr, 0.0, a.data(), b.data()));
    REQUIRE (a == in);
    REQUIRE (b == r);

    REQUIRE (shifter.shift (in.data(), r.data(), n, kSr, -5.0, a.data(), b.data()));
    REQUIRE (shifter.shift (in.data(), r.data(), n, kSr, -5.0, c.data(), d.data()));
    REQUIRE (a == c);
    REQUIRE (b == d);
}

TEST_CASE ("cancel stops a render", "[shifter]")
{
    const int64_t n = (int64_t) kSr * 2;
    std::vector<float> in ((size_t) n, 0.1f), outL ((size_t) n), outR ((size_t) n);
    std::atomic<bool> cancel { true };
    SignalsmithPitchShifter shifter;
    REQUIRE_FALSE (shifter.shift (in.data(), in.data(), n, kSr, 2.0, outL.data(), outR.data(), &cancel));
}

#include <cstdio>
#include <signalsmith-stretch/signalsmith-stretch.h>

TEST_CASE ("experiment: shifter pitch accuracy vs settings", "[.][experiment]")
{
    const int64_t n = (int64_t) kSr * 3;
    std::vector<float> in ((size_t) n), outL ((size_t) n), outR ((size_t) n);
    for (int64_t i = 0; i < n; ++i) in[(size_t) i] = 0.5f * (float) std::sin (2.0 * M_PI * 440.0 * (double) i / kSr);
    std::printf ("unshifted measures %.4f Hz\n", dominantFrequency (in, (size_t) kSr, 65536));

    struct Cfg { int block, interval; };
    for (Cfg cfg : { Cfg { 0, 0 }, Cfg { 4096, 1024 }, Cfg { 8192, 2048 }, Cfg { 16384, 4096 }, Cfg { 8192, 1024 } })
        for (double semis : { 3.0, -3.0, 7.0, 12.0, 0.5, 1.0, -1.0 })
        {
            const double tonality = 8000.0 / kSr;
            signalsmith::stretch::SignalsmithStretch<float> st (1);
            if (cfg.block == 0) st.presetDefault (2, (float) kSr); else st.configure (2, cfg.block, cfg.interval);
            st.setTransposeSemitones ((float) semis, (float) tonality);
            const int inLat = st.inputLatency(), outLat = st.outputLatency();
            std::vector<float> seekL ((size_t) inLat), seekR ((size_t) inLat);
            std::copy (in.begin(), in.begin() + inLat, seekL.begin()); seekR = seekL;
            float* seekP[2] = { seekL.data(), seekR.data() };
            st.seek (seekP, inLat, 1.0);
            std::vector<float> bl (4096), br (4096), ol (4096), orr (4096);
            float* ip[2] = { bl.data(), br.data() }; float* op[2] = { ol.data(), orr.data() };
            int64_t inPos = inLat, outPos = 0;
            const int64_t totalOut = n + outLat;
            while (outPos < totalOut)
            {
                const int k = (int) std::min<int64_t> (4096, totalOut - outPos);
                for (int i = 0; i < k; ++i) { const int64_t q = inPos + i; bl[(size_t) i] = br[(size_t) i] = q < n ? in[(size_t) q] : 0.0f; }
                st.process (ip, k, op, k);
                inPos += k;
                for (int i = 0; i < k; ++i) { const int64_t o = outPos + i - outLat; if (o >= 0 && o < n) outL[(size_t) o] = ol[(size_t) i]; }
                outPos += k;
            }
            const double f = dominantFrequency (outL, (size_t) kSr, 65536);
            const double target = 440.0 * std::pow (2.0, semis / 12.0);
            std::printf ("block %5d/%4d  semis %5.1f  -> %9.3f Hz  (%+.2f cents)  latency in %d out %d\n",
                         cfg.block, cfg.interval, semis, f, 1200.0 * std::log2 (f / target), inLat, outLat);
        }
}

#include <thread>

TEST_CASE ("experiment: render determinism vs thread and buffer alignment", "[.][experiment]")
{
    const int64_t n = 40000;
    std::vector<float> in ((size_t) n);
    for (int64_t i = 0; i < n; ++i) in[(size_t) i] = 0.4f * (float) std::sin (0.013 * (double) i) + 0.2f * (float) std::sin (0.071 * (double) i);

    auto render = [&] (size_t offset) {
        std::vector<float> inShift ((size_t) n + offset), oL ((size_t) n + offset), oR ((size_t) n + offset);
        std::copy (in.begin(), in.end(), inShift.begin() + (long) offset);
        SignalsmithPitchShifter sh;
        sh.shift (inShift.data() + offset, inShift.data() + offset, n, kSr, -5.0, oL.data() + offset, oR.data() + offset);
        return std::vector<float> (oL.begin() + (long) offset, oL.end());
    };
    auto maxDiff = [] (const std::vector<float>& a, const std::vector<float>& b) {
        float m = 0; for (size_t i = 0; i < a.size(); ++i) m = std::max (m, std::fabs (a[i] - b[i])); return m; };

    const auto base = render (0);
    std::printf ("same thread, offset 0 again:   %.3g\n", maxDiff (base, render (0)));
    std::printf ("same thread, offset 1:         %.3g\n", maxDiff (base, render (1)));
    std::printf ("same thread, offset 3:         %.3g\n", maxDiff (base, render (3)));
    std::vector<float> other;
    std::thread t ([&] { other = render (0); });
    t.join();
    std::printf ("other thread, offset 0:        %.3g\n", maxDiff (base, other));
    std::thread t2 ([&] { other = render (1); });
    t2.join();
    std::printf ("other thread, offset 1:        %.3g\n", maxDiff (base, other));
}
