#include "TestFramework.h"

#include "common/SignalGen.h"
#include "dsp/Decimator.h"
#include "dsp/Fft.h"
#include "dsp/InputConditioner.h"
#include "dsp/VoicingDetector.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace evox;

namespace
{
/** Steady-state gain of `proc` at `hz`, measured as output RMS / input RMS. */
template <typename Proc>
double measureGain (Proc&& proc, double fs, double hz)
{
    const auto sig = tools::sine (fs, 1.0, tools::constantPitch (hz), 0.5f);
    double in = 0.0, out = 0.0;
    for (size_t i = 0; i < sig.samples.size(); ++i)
    {
        const float y = proc (sig.samples[i]);
        if (i > sig.samples.size() / 2)
        {
            in += double (sig.samples[i]) * sig.samples[i];
            out += double (y) * y;
        }
    }
    return std::sqrt (out / in);
}
} // namespace

TEST_CASE ("fft: matches naive DFT and round-trips")
{
    constexpr int order = 6, n = 1 << order;
    dsp::Fft fft;
    fft.prepare (order);

    std::vector<float> re (n), im (n), re0 (n), im0 (n);
    for (int i = 0; i < n; ++i)
    {
        re[i] = re0[i] = std::sin (0.37f * i) + 0.2f * std::cos (1.9f * i);
        im[i] = im0[i] = 0.1f * i / n;
    }

    fft.perform (re.data(), im.data(), false);

    double maxErr = 0.0;
    for (int k = 0; k < n; ++k)
    {
        std::complex<double> acc;
        for (int t = 0; t < n; ++t)
            acc += std::complex<double> (re0[t], im0[t]) * std::polar (1.0, -2.0 * std::numbers::pi * k * t / n);
        maxErr = std::max (maxErr, std::abs (acc - std::complex<double> (re[k], im[k])));
    }
    CHECK (maxErr < 1.0e-4);

    fft.perform (re.data(), im.data(), true);
    for (int i = 0; i < n; ++i)
    {
        CHECK_NEAR (re[i], re0[i], 1.0e-5f);
        CHECK_NEAR (im[i], im0[i], 1.0e-5f);
    }
}

TEST_CASE ("conditioning: DC blocker removes offset, passes audio band")
{
    dsp::DcBlocker dc;
    dc.prepare (48000.0);

    double tailMean = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const float y = dc.process (0.5f);
        if (i >= 47000)
            tailMean += y / 1000.0;
    }
    CHECK (std::abs (tailMean) < 1.0e-3);

    dc.reset();
    const auto g = measureGain ([&] (float x) { return dc.process (x); }, 48000.0, 1000.0);
    CHECK_NEAR (g, 1.0, 0.001);
}

TEST_CASE ("conditioning: 60 Hz high-pass response")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        dsp::InputConditioner c;
        c.prepare (fs);
        c.setLowCutEnabled (true);
        const auto g20 = measureGain ([&] (float x) { return c.process (x); }, fs, 20.0);
        c.reset();
        const auto g60 = measureGain ([&] (float x) { return c.process (x); }, fs, 60.0);
        c.reset();
        const auto g1k = measureGain ([&] (float x) { return c.process (x); }, fs, 1000.0);

        CHECK_MSG (20.0 * std::log10 (g20) < -18.0, "20 Hz should be strongly attenuated");
        CHECK_NEAR (20.0 * std::log10 (g60), -3.0, 0.5);
        CHECK_NEAR (20.0 * std::log10 (g1k), 0.0, 0.05);

        c.setLowCutEnabled (false);
        c.reset();
        const auto g20off = measureGain ([&] (float x) { return c.process (x); }, fs, 20.0);
        CHECK_NEAR (20.0 * std::log10 (g20off), 0.0, 0.3); // only the 5 Hz DC blocker remains
    }
}

TEST_CASE ("conditioning: decimator keeps analysis rate >= 20 kHz")
{
    CHECK (dsp::Decimator::factorFor (44100.0) == 2);
    CHECK (dsp::Decimator::factorFor (48000.0) == 2);
    CHECK (dsp::Decimator::factorFor (88200.0) == 4);
    CHECK (dsp::Decimator::factorFor (96000.0) == 4);
    CHECK (dsp::Decimator::factorFor (192000.0) == 8);
    CHECK (dsp::Decimator::factorFor (32000.0) == 1);
}

TEST_CASE ("voicing: candidate rules and hysteresis")
{
    dsp::VoicingDetector v;
    v.setOnsetFrames (2);
    v.setReleaseFrames (3);

    dsp::VoicingDetector::Features voiced { -20.0f, 400.0f, 220.0f, true };
    dsp::VoicingDetector::Features quiet { -70.0f, 400.0f, 220.0f, true };
    dsp::VoicingDetector::Features hiss { -20.0f, 5000.0f, 220.0f, true };
    dsp::VoicingDetector::Features aperiodic { -20.0f, 400.0f, 220.0f, false };

    CHECK (v.isCandidate (voiced));
    CHECK (! v.isCandidate (quiet));
    CHECK (! v.isCandidate (hiss));
    CHECK (! v.isCandidate (aperiodic));

    CHECK (! v.update (voiced)); // onset needs 2 frames
    CHECK (v.update (voiced));
    CHECK (v.update (hiss));     // release needs 3 frames
    CHECK (v.update (voiced));   // a single good frame resets the release counter
    CHECK (v.update (hiss));
    CHECK (v.update (hiss));
    CHECK (! v.update (hiss));
}
