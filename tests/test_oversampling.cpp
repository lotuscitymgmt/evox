#include "TestFramework.h"
#include "ShifterTestUtils.h"

#include "dsp/Oversampler2x.h"

#include <algorithm>
#include <string>

using namespace evox;

namespace
{
/** Gain (dB) of an up -> down round trip for a sine at `hz`. */
double roundTripGainDb (double fs, double hz)
{
    dsp::Upsampler2x up;
    dsp::Downsampler2x down;
    const auto sig = tools::sine (fs, 0.3, tools::constantPitch (hz), 0.5f);
    double in = 0.0, out = 0.0;
    for (size_t i = 0; i < sig.samples.size(); ++i)
    {
        float a, b;
        up.process (sig.samples[i], a, b);
        const float y = down.process (a, b);
        if (i > 2000)
        {
            in += double (sig.samples[i]) * sig.samples[i];
            out += double (y) * y;
        }
    }
    return 10.0 * std::log10 (out / in);
}

/** Level (dB re. input) of the image an upsampler leaves at 2fs - hz. */
double imageRejectionDb (double fs, double hz)
{
    dsp::Upsampler2x up;
    const auto sig = tools::sine (fs, 0.3, tools::constantPitch (hz), 0.5f);
    std::vector<float> x2;
    for (float s : sig.samples)
    {
        float a, b;
        up.process (s, a, b);
        x2.push_back (a);
        x2.push_back (b);
    }
    const double image = fs - hz; // mirror around the base Nyquist, at the 2x rate
    auto amplitude = [&] (double f) {
        double re = 0.0, im = 0.0;
        for (size_t i = 4000; i < x2.size(); ++i)
        {
            const double ph = 2.0 * std::numbers::pi * f * static_cast<double> (i) / (2.0 * fs);
            re += x2[i] * std::cos (ph);
            im += x2[i] * std::sin (ph);
        }
        return std::sqrt (re * re + im * im);
    };
    return 20.0 * std::log10 (amplitude (image) / amplitude (hz));
}
} // namespace

TEST_CASE ("oversampling: half-band round trip is flat and rejects images")
{
    const double fs = 48000.0;
    for (double hz : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 19000.0 })
        CHECK_MSG (std::abs (roundTripGainDb (fs, hz)) < 0.1, std::to_string (hz) + " Hz: " + std::to_string (roundTripGainDb (fs, hz)));
    CHECK (std::abs (roundTripGainDb (fs, 20000.0)) < 0.5);

    for (double hz : { 1000.0, 10000.0, 18000.0 })
        CHECK_MSG (imageRejectionDb (fs, hz) < -65.0, std::to_string (hz) + " Hz image " + std::to_string (imageRejectionDb (fs, hz)) + " dB");
}

TEST_CASE ("oversampling: round trip delay is exactly the reserved latency")
{
    dsp::Upsampler2x up;
    dsp::Downsampler2x down;
    std::vector<float> out;
    for (int i = 0; i < 200; ++i)
    {
        float a, b;
        up.process (i == 10 ? 1.0f : 0.0f, a, b);
        out.push_back (down.process (a, b));
    }
    const auto peak = std::max_element (out.begin(), out.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }) - out.begin();
    CHECK_MSG (peak == 10 + dsp::EvoxEngine::oversamplingDelay, std::to_string (peak));
}

TEST_CASE ("oversampling: 1x and 2x render the same pitch and level")
{
    const double fs = 48000.0;
    const auto in = tools::synthVoice (fs, 1.2, tools::constantPitch (210.0));

    std::vector<float> outs[2];
    for (int os = 0; os < 2; ++os)
    {
        dsp::EngineParams p;
        p.oversample2x = os == 1;
        p.correction.transpose = 5.0f;
        outs[os] = test::renderEngine (in.samples, fs, p);
        const float hz = test::medianHz (test::pitchTrack (outs[os], fs, dsp::InputType::altoTenor, 0.2));
        CHECK_MSG (std::abs (dsp::centsBetween (277.18f, hz)) < 3.0f, "os " + std::to_string (os) + ": " + std::to_string (hz)); // G#3 + 5 st = C#4
    }

    // (Waveform phase is set by each stage's synthesis-grain grid, so the two outputs may
    // differ by a fraction of a period; timing is checked on onsets below.)

    const double levelDiff = 20.0 * std::log10 (test::rms (outs[1], 20000, outs[1].size()) / test::rms (outs[0], 20000, outs[0].size()));
    CHECK_MSG (std::abs (levelDiff) < 1.0, std::to_string (levelDiff) + " dB");
}

TEST_CASE ("oversampling: note onsets land at the reported latency in both modes")
{
    const double fs = 48000.0;
    const auto in = tools::concat ({ tools::silence (fs, 0.3), tools::synthVoice (fs, 0.6, tools::constantPitch (247.0)) });

    auto onset = [&] (const std::vector<float>& v) {
        float env = 0.0f, peak = 0.0f;
        std::vector<float> e (v.size());
        for (size_t i = 0; i < v.size(); ++i)
        {
            env = std::max (std::abs (v[i]), env * 0.995f);
            e[i] = env;
            peak = std::max (peak, env);
        }
        for (size_t i = 0; i < e.size(); ++i)
            if (e[i] > 0.2f * peak)
                return static_cast<long> (i);
        return -1L;
    };

    const long inOnset = onset (in.samples);
    long onsets[2] {};
    int latency = 0;
    for (int os = 0; os < 2; ++os)
    {
        dsp::EngineParams p;
        p.oversample2x = os == 1;
        p.correction.transpose = 3.0f;
        onsets[os] = onset (test::renderEngine (in.samples, fs, p, &latency)) - latency;
    }
    const std::string msg = "input " + std::to_string (inOnset) + " | 1x " + std::to_string (onsets[0]) + " | 2x " + std::to_string (onsets[1]);
    std::printf ("    onsets (samples): %s\n", msg.c_str());
    CHECK_MSG (std::abs (onsets[1] - onsets[0]) <= 48, msg);       // 1x vs 2x within 1 ms
    for (long o : onsets)
        CHECK_MSG (std::abs (o - inOnset) <= 240, msg);             // vs. latency-compensated input within 5 ms
}

TEST_CASE ("oversampling: 2x reduces aliasing from compressed grains")
{
    // Throat 55% compresses grains ~1.8x: a bright source folds its top octave back down at 1x.
    const double fs = 48000.0;
    const double f0 = 196.0; // G3, stays on-note
    const auto in = tools::sawtooth (fs, 1.2, tools::constantPitch (f0), 0.5f);

    auto inharmonicDb = [&] (bool os) {
        dsp::EngineParams p;
        p.oversample2x = os;
        p.shifter.throat = 0.55f;
        p.shifter.character = 0.0f;
        p.correction.noteModes.fill (dsp::NoteMode::bypassed);
        const auto out = test::renderEngine (in.samples, fs, p);

        dsp::Fft fft;
        fft.prepare (15);
        const int n = 1 << 15;
        std::vector<float> re (n), im (n, 0.0f);
        for (int i = 0; i < n; ++i)
            re[static_cast<size_t> (i)] = out[12000 + static_cast<size_t> (i)] * (0.5f - 0.5f * std::cos (2.0f * std::numbers::pi_v<float> * i / n));
        fft.perform (re.data(), im.data(), false);

        // Energy away from harmonics (> 25 Hz from any k*f0) vs total, 2-20 kHz.
        double off = 0.0, total = 0.0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double hz = k * fs / n;
            if (hz < 2000.0 || hz > 20000.0)
                continue;
            const double e = double (re[static_cast<size_t> (k)]) * re[static_cast<size_t> (k)] + double (im[static_cast<size_t> (k)]) * im[static_cast<size_t> (k)];
            const double nearest = std::round (hz / f0) * f0;
            total += e;
            if (std::abs (hz - nearest) > 25.0)
                off += e;
        }
        return 10.0 * std::log10 (off / total);
    };

    const double at1x = inharmonicDb (false), at2x = inharmonicDb (true);
    std::printf ("    inharmonic (aliased) energy, throat 55%%: 1x %.1f dB, 2x %.1f dB\n", at1x, at2x);
    CHECK_MSG (at2x < at1x - 3.0, "inharmonic energy 1x " + std::to_string (at1x) + " dB, 2x " + std::to_string (at2x) + " dB");
}

TEST_CASE ("oversampling: switching mid-stream is click-free and allocation-free")
{
    const double fs = 48000.0;
    const auto in = tools::synthVoice (fs, 1.5, tools::constantPitch (220.0));
    dsp::EvoxEngine engine;
    engine.prepare (fs, 128, 1);
    dsp::EngineParams p;
    engine.setParams (p);

    std::vector<float> out = in.samples;
    for (size_t start = 0; start + 128 <= out.size(); start += 128)
    {
        if (start == 128 * 150 || start == 128 * 350)
        {
            p.oversample2x = ! p.oversample2x;
            engine.setParams (p);
        }
        float* ptr[1] = { out.data() + start };
        engine.process (ptr, 1, 128);
    }
    CHECK (! engine.isOversamplingActive()); // toggled twice

    // Largest sample-to-sample jump around the switches stays in line with the signal.
    auto maxStep = [&] (size_t from, size_t to) {
        float m = 0.0f;
        for (size_t i = from + 1; i < to; ++i)
            m = std::max (m, std::abs (out[i] - out[i - 1]));
        return m;
    };
    const float steady = maxStep (10000, 18000);
    const float aroundSwitch = std::max (maxStep (128 * 150, 128 * 150 + 3000), maxStep (128 * 350, 128 * 350 + 3000));
    CHECK_MSG (aroundSwitch < 1.5f * steady, std::to_string (aroundSwitch) + " vs steady " + std::to_string (steady));
}
