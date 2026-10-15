#include "TestFramework.h"
#include "PitchTestUtils.h"

#include "dsp/YinPitchDetector.h"

#include <string>

using namespace evox;
using dsp::InputType;

namespace
{
std::string describe (const test::TrackStats& s)
{
    return "voiced " + std::to_string (s.voicedRatio()) + " p50 " + std::to_string (s.percentile (0.5f))
         + "ct p95 " + std::to_string (s.percentile (0.95f)) + "ct octErr " + std::to_string (s.octaveErrors);
}
} // namespace

TEST_CASE ("yin: single-frame accuracy on pure sines")
{
    for (double rate : { 22050.0, 24000.0 })
    {
        dsp::YinPitchDetector yin;
        yin.prepare (rate, 45.0f);
        yin.configure (45.0f, 2000.0f);

        for (float hz : { 50.0f, 82.4f, 110.0f, 196.0f, 440.0f, 880.0f, 1500.0f })
        {
            const auto sig = tools::sine (rate, 0.2, tools::constantPitch (hz));
            const auto r = yin.analyse (sig.samples.data() + 100, 0.15f);
            CHECK_MSG (r.belowThreshold, std::to_string (hz));
            CHECK_MSG (std::abs (dsp::centsBetween (hz, r.f0Hz)) < 2.0f,
                       std::to_string (hz) + " -> " + std::to_string (r.f0Hz));
        }
    }
}

TEST_CASE ("yin: silence is not periodic")
{
    dsp::YinPitchDetector yin;
    yin.prepare (24000.0, 45.0f);
    std::vector<float> zeros (static_cast<size_t> (yin.getFrameLength()), 0.0f);
    const auto r = yin.analyse (zeros.data(), 0.2f);
    CHECK (! r.belowThreshold);
}

TEST_CASE ("analyzer: steady sines and saws per input type, 44.1/48/96 kHz")
{
    struct Case { InputType type; float hz; };
    const Case cases[] = {
        { InputType::soprano, 262.0f },  { InputType::soprano, 1000.0f },
        { InputType::altoTenor, 98.0f }, { InputType::altoTenor, 440.0f }, { InputType::altoTenor, 698.0f },
        { InputType::lowMale, 58.0f },   { InputType::lowMale, 110.0f },   { InputType::lowMale, 330.0f },
        { InputType::instrument, 50.0f },{ InputType::instrument, 1760.0f },
    };

    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        for (const auto& c : cases)
        {
            const std::string label = std::to_string (fs) + "Hz type " + std::to_string (int (c.type)) + " f0 " + std::to_string (c.hz);

            const auto s1 = test::track (tools::sine (fs, 0.5, tools::constantPitch (c.hz)), fs, c.type);
            CHECK_MSG (s1.voicedRatio() > 0.97f, "sine " + label + " " + describe (s1));
            // Perceptual pitch JND is ~5-10 ct; demand far better than that for steady tones.
            // Very short periods (>1.2 kHz, ~15 analysis samples) carry a small interpolation bias.
            const float p95Limit = c.hz > 1200.0f ? 5.0f : 2.0f;
            CHECK_MSG (s1.percentile (0.95f) < p95Limit && s1.percentile (1.0f) < 6.0f, "sine " + label + " " + describe (s1));

            const auto s2 = test::track (tools::sawtooth (fs, 0.5, tools::constantPitch (c.hz)), fs, c.type);
            CHECK_MSG (s2.voicedRatio() > 0.97f, "saw " + label + " " + describe (s2));
            CHECK_MSG (s2.percentile (0.95f) < p95Limit && s2.percentile (1.0f) < 6.0f, "saw " + label + " " + describe (s2));
        }
    }
}

TEST_CASE ("analyzer: log sweeps (sine + saw) track without octave errors")
{
    struct Case { InputType type; double from, to; };
    const Case cases[] = {
        { InputType::soprano, 180.0, 1000.0 },
        { InputType::altoTenor, 90.0, 700.0 },
        { InputType::lowMale, 60.0, 350.0 },
        { InputType::instrument, 50.0, 1800.0 },
    };

    for (double fs : { 44100.0, 48000.0 })
    {
        for (const auto& c : cases)
        {
            for (int saw = 0; saw < 2; ++saw)
            {
                // Down-and-up so both directions are exercised: 2 s up then 2 s down.
                auto up = tools::logSweep (c.from, c.to, 2.0);
                auto curve = [=] (double t) { return t < 2.0 ? up (t) : up (4.0 - t); };
                const auto sig = saw ? tools::sawtooth (fs, 4.0, curve) : tools::sine (fs, 4.0, curve);
                const auto s = test::track (sig, fs, c.type);
                const std::string label = (saw ? "saw " : "sine ") + std::to_string (c.from) + "-" + std::to_string (c.to)
                                        + " @" + std::to_string (fs) + " " + describe (s);

                CHECK_MSG (s.voicedRatio() > 0.97f, label);
                CHECK_MSG (s.octaveErrors == 0, label);
                // A ~1 octave/s glide moves ~5 ct per ms, so allow for window-centre ambiguity.
                CHECK_MSG (s.percentile (0.5f) < 6.0f, label);
                CHECK_MSG (s.percentile (0.95f) < 25.0f, label);
            }
        }
    }
}

TEST_CASE ("analyzer: synthetic voice with vibrato, jitter and breath")
{
    for (auto [type, hz] : { std::pair { InputType::lowMale, 98.0 }, std::pair { InputType::altoTenor, 220.0 },
                             std::pair { InputType::soprano, 523.0 } })
    {
        const double fs = 48000.0;
        const auto sig = tools::synthVoice (fs, 2.0, tools::withVibrato (tools::constantPitch (hz), 5.5, 50.0), 0.02f);
        const auto s = test::track (sig, fs, type);
        const std::string label = std::to_string (hz) + " " + describe (s);
        CHECK_MSG (s.voicedRatio() > 0.95f, label);
        CHECK_MSG (s.octaveErrors == 0, label);
        CHECK_MSG (s.percentile (0.5f) < 5.0f, label);
        CHECK_MSG (s.percentile (0.95f) < 15.0f, label);
    }
}

TEST_CASE ("analyzer: weak fundamental (missing-ish f0) does not jump an octave")
{
    // Fundamental 12 dB below the 2nd harmonic: a classic octave-error trap.
    const double fs = 48000.0;
    auto amps = [] (int k) { return k == 1 ? 0.25 : (k == 2 ? 1.0 : 0.5 / k); };
    const auto sig = tools::harmonicTone (fs, 1.0, tools::constantPitch (130.0), amps);

    for (float tracking : { 0.0f, 0.5f })
    {
        const auto s = test::track (sig, fs, InputType::altoTenor, tracking);
        CHECK_MSG (s.octaveErrors == 0, "tracking " + std::to_string (tracking) + " " + describe (s));
        CHECK_MSG (s.voicedRatio() > 0.95f, describe (s));
    }
}

TEST_CASE ("analyzer: looser tracking follows a breathy voice better")
{
    const double fs = 48000.0;
    const auto sig = tools::synthVoice (fs, 2.0, tools::constantPitch (200.0), 0.35f, 3);
    const auto tight = test::track (sig, fs, InputType::altoTenor, 0.0f);
    const auto loose = test::track (sig, fs, InputType::altoTenor, 1.0f);
    CHECK_MSG (loose.voicedRatio() >= tight.voicedRatio(), describe (tight) + " | " + describe (loose));
    CHECK_MSG (loose.voicedRatio() > 0.9f, describe (loose));
    CHECK_MSG (loose.octaveErrors == 0, describe (loose));
}

TEST_CASE ("analyzer: noise, sibilance and silence are unvoiced")
{
    const double fs = 48000.0;
    for (float tracking : { 0.0f, 0.5f, 1.0f })
    {
        for (int kind = 0; kind < 3; ++kind)
        {
            const auto sig = kind == 0 ? tools::whiteNoise (fs, 1.0)
                           : kind == 1 ? tools::sibilant (fs, 1.0)
                                       : tools::silence (fs, 1.0);
            int voiced = 0, total = 0;
            test::track (sig, fs, InputType::altoTenor, tracking, 0.06, &voiced, &total);
            CHECK_MSG (total > 100, "frames analysed");
            CHECK_MSG (voiced <= total / 50, "kind " + std::to_string (kind) + " tracking " + std::to_string (tracking)
                                                 + ": " + std::to_string (voiced) + "/" + std::to_string (total));
        }
    }
}

TEST_CASE ("analyzer: vowel -> 's' -> vowel transitions")
{
    const double fs = 48000.0;
    const auto sig = tools::concat ({ tools::synthVoice (fs, 0.4, tools::constantPitch (196.0)),
                                      tools::sibilant (fs, 0.25, 0.2f),
                                      tools::synthVoice (fs, 0.4, tools::constantPitch (247.0), 0.02f, 5) });
    int voicedOnS = 0, sFrames = 0;
    const auto s = test::track (sig, fs, InputType::altoTenor, 0.5f, 0.06, &voicedOnS, &sFrames);

    CHECK_MSG (voicedOnS <= sFrames / 10, std::to_string (voicedOnS) + "/" + std::to_string (sFrames));
    CHECK_MSG (s.voicedRatio() > 0.85f, describe (s)); // edges lose a few frames to hysteresis
    CHECK_MSG (s.octaveErrors == 0, describe (s));
}

TEST_CASE ("analyzer: input type switch is allocation-free and immediate")
{
    dsp::PitchAnalyzer a;
    a.prepare (48000.0);
    a.setInputType (InputType::soprano);
    const int sopranoDelay = a.getAnalysisDelaySamples();
    a.setInputType (InputType::lowMale);
    const int lowDelay = a.getAnalysisDelaySamples();
    CHECK (lowDelay > sopranoDelay);
    CHECK (a.getHopSizeHostSamples() == 128);
}
