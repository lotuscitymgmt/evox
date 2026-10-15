#pragma once

// Deterministic test-signal generators with known ground-truth f0.

#include <cmath>
#include <functional>
#include <numbers>
#include <random>
#include <vector>

namespace evox::tools
{

struct TestSignal
{
    std::vector<float> samples;
    std::vector<float> trueHz; // instantaneous f0 per sample, 0 where unpitched
};

using PitchCurve = std::function<double (double /*seconds*/)>;

inline PitchCurve constantPitch (double hz) { return [hz] (double) { return hz; }; }

inline PitchCurve logSweep (double fromHz, double toHz, double seconds)
{
    return [=] (double t) { return fromHz * std::pow (toHz / fromHz, std::min (t, seconds) / seconds); };
}

inline PitchCurve withVibrato (PitchCurve base, double rateHz, double depthCents)
{
    return [=] (double t) {
        return base (t) * std::exp2 (depthCents / 1200.0 * std::sin (2.0 * std::numbers::pi * rateHz * t));
    };
}

/** Band-limited harmonic oscillator. amplitudeOf(k) gives the k-th harmonic's level. */
inline TestSignal harmonicTone (double fs, double seconds, const PitchCurve& pitch,
                                const std::function<double (int)>& amplitudeOf, float gain = 0.5f)
{
    const auto n = static_cast<size_t> (fs * seconds);
    TestSignal s { std::vector<float> (n), std::vector<float> (n) };
    double phase = 0.0, norm = 0.0;

    for (int k = 1; k <= 64; ++k)
        norm += std::abs (amplitudeOf (k));

    for (size_t i = 0; i < n; ++i)
    {
        const double f = pitch (static_cast<double> (i) / fs);
        double y = 0.0;
        const int maxK = static_cast<int> (0.45 * fs / f);
        for (int k = 1; k <= maxK; ++k)
            y += amplitudeOf (k) * std::sin (k * phase);
        s.samples[i] = static_cast<float> (gain * y / std::max (norm, 1.0));
        s.trueHz[i] = static_cast<float> (f);
        phase = std::fmod (phase + 2.0 * std::numbers::pi * f / fs, 2.0 * std::numbers::pi);
    }
    return s;
}

inline TestSignal sine (double fs, double seconds, const PitchCurve& pitch, float gain = 0.5f)
{
    return harmonicTone (fs, seconds, pitch, [] (int k) { return k == 1 ? 1.0 : 0.0; }, gain);
}

inline TestSignal sawtooth (double fs, double seconds, const PitchCurve& pitch, float gain = 0.5f)
{
    return harmonicTone (fs, seconds, pitch, [] (int k) { return 1.0 / k; }, gain);
}

/** Crude but useful vowel model: glottal-like source (-12 dB/oct) through three
    resonators (/a/ formants), plus jitter, shimmer and breath noise. */
inline TestSignal synthVoice (double fs, double seconds, const PitchCurve& pitch,
                              float breathiness = 0.02f, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> gauss (0.0, 1.0);

    double jitterState = 0.0;
    auto jittered = [&] (double t) {
        jitterState = 0.995 * jitterState + 0.005 * gauss (rng);
        return pitch (t) * (1.0 + 0.004 * jitterState);
    };

    auto src = harmonicTone (fs, seconds, jittered, [] (int k) { return 1.0 / (k * k); }, 1.0f);

    struct Resonator
    {
        double a = 0, b = 0, c = 0, y1 = 0, y2 = 0;
        Resonator (double fsr, double f, double bw)
        {
            c = -std::exp (-2.0 * std::numbers::pi * bw / fsr);
            b = 2.0 * std::exp (-std::numbers::pi * bw / fsr) * std::cos (2.0 * std::numbers::pi * f / fsr);
            a = 1.0 - b - c;
        }
        double process (double x)
        {
            const double y = a * x + b * y1 + c * y2;
            y2 = y1;
            y1 = y;
            return y;
        }
    };

    Resonator f1 (fs, 730, 90), f2 (fs, 1090, 110), f3 (fs, 2440, 160);
    double peak = 1.0e-9;
    std::vector<double> tmp (src.samples.size());

    for (size_t i = 0; i < tmp.size(); ++i)
    {
        const double shimmer = 1.0 + 0.05 * std::sin (2.0 * std::numbers::pi * 3.1 * i / fs);
        const double x = src.samples[i] * shimmer;
        tmp[i] = f1.process (x) + 0.5 * f2.process (x) + 0.25 * f3.process (x) + breathiness * gauss (rng);
        peak = std::max (peak, std::abs (tmp[i]));
    }

    for (size_t i = 0; i < tmp.size(); ++i)
        src.samples[i] = static_cast<float> (0.5 * tmp[i] / peak);

    return src;
}

inline TestSignal whiteNoise (double fs, double seconds, float gain = 0.3f, unsigned seed = 7)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    const auto n = static_cast<size_t> (fs * seconds);
    TestSignal s { std::vector<float> (n), std::vector<float> (n, 0.0f) };
    for (auto& x : s.samples)
        x = gain * uni (rng);
    return s;
}

/** "sss"-like noise: white noise through a first-difference (bright, high ZCR). */
inline TestSignal sibilant (double fs, double seconds, float gain = 0.3f, unsigned seed = 11)
{
    auto s = whiteNoise (fs, seconds, gain, seed);
    float prev = 0.0f;
    for (auto& x : s.samples)
    {
        const float y = 0.5f * (x - prev);
        prev = x;
        x = y;
    }
    return s;
}

inline TestSignal silence (double fs, double seconds)
{
    const auto n = static_cast<size_t> (fs * seconds);
    return { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
}

inline TestSignal concat (std::initializer_list<TestSignal> parts)
{
    TestSignal out;
    for (const auto& p : parts)
    {
        out.samples.insert (out.samples.end(), p.samples.begin(), p.samples.end());
        out.trueHz.insert (out.trueHz.end(), p.trueHz.begin(), p.trueHz.end());
    }
    return out;
}

} // namespace evox::tools
