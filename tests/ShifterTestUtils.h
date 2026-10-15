#pragma once

#include "PitchTestUtils.h"
#include "dsp/EvoxEngine.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <array>

#include <cmath>
#include <vector>

namespace evox::test
{

/** Renders a mono signal through a fully configured engine (128-sample blocks). */
inline std::vector<float> renderEngine (const std::vector<float>& in, double fs, const dsp::EngineParams& params,
                                        int* latencyOut = nullptr)
{
    dsp::EvoxEngine engine;
    engine.prepare (fs, 128, 1);
    engine.setParams (params);
    std::vector<float> out = in;
    for (size_t start = 0; start < out.size(); start += 128)
    {
        float* ptr[1] = { out.data() + start };
        engine.process (ptr, 1, static_cast<int> (std::min<size_t> (128, out.size() - start)));
    }
    if (latencyOut)
        *latencyOut = engine.getLatencySamples();
    return out;
}

struct PitchPoint
{
    double seconds;
    float hz;
};

/** Pitch track of an arbitrary signal using the same analyzer (voiced frames only). */
inline std::vector<PitchPoint> pitchTrack (const std::vector<float>& sig, double fs, dsp::InputType type, double from = 0.1)
{
    dsp::PitchAnalyzer a;
    a.prepare (fs);
    a.setInputType (type);
    a.setTracking (0.3f);
    std::vector<PitchPoint> points;
    for (size_t i = 0; i < sig.size(); ++i)
        if (a.push (sig[i]))
        {
            const auto& e = a.getEstimate();
            const double t = static_cast<double> (e.centreSample) / fs;
            if (e.voiced && t >= from)
                points.push_back ({ t, e.f0Hz });
        }
    return points;
}

inline float medianHz (std::vector<PitchPoint> pts)
{
    if (pts.empty())
        return 0.0f;
    std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.hz < b.hz; });
    return pts[pts.size() / 2].hz;
}

inline double rms (const std::vector<float>& v, size_t from, size_t to)
{
    double acc = 0.0;
    for (size_t i = from; i < to; ++i)
        acc += double (v[i]) * v[i];
    return std::sqrt (acc / static_cast<double> (to - from));
}

/** Formant-envelope centroid of a harmonic signal with known f0. Each harmonic's level is
    the strongest spectral peak within +/-3 % of k * f0, capped at +/-30 % of f0 (so pitch jitter, as in a real voice,
    can't make it miss the peak); the -12 dB/oct glottal tilt of the test source is undone
    (x k^2) and the power-weighted centroid of that envelope is returned. Unlike a plain
    spectral centroid this ignores where the harmonics sit, so it moves only when the
    envelope (formants) moves. */
inline double harmonicEnvelopeCentroid (const std::vector<float>& v, size_t from, size_t n, double fs, double f0,
                                        double loHz = 200.0, double hiHz = 3500.0)
{
    dsp::Fft fft;
    fft.prepare (15);
    const int size = 1 << 15; // zero-padded: ~1.5 Hz bins at 48 kHz
    std::vector<float> re (static_cast<size_t> (size), 0.0f), im (static_cast<size_t> (size), 0.0f);
    for (size_t i = 0; i < n && i < static_cast<size_t> (size); ++i)
        re[i] = v[from + i] * static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n)));
    fft.perform (re.data(), im.data(), false);

    const double binHz = fs / size;
    double num = 0.0, den = 0.0;
    for (int k = 1; k * f0 <= hiHz; ++k)
    {
        const double f = k * f0;
        if (f < loHz)
            continue;
        // Search +/-3 % for jitter, but never wider than +/-30 % of the harmonic spacing.
        const double halfWidth = std::max (1.5 * binHz, std::min (0.03 * f, 0.3 * f0));
        const int lo = static_cast<int> (std::floor ((f - halfWidth) / binHz));
        const int hi = static_cast<int> (std::ceil ((f + halfWidth) / binHz));
        double peak = 0.0;
        for (int b = lo; b <= hi && b < size / 2; ++b)
            peak = std::max (peak, double (re[static_cast<size_t> (b)]) * re[static_cast<size_t> (b)] + double (im[static_cast<size_t> (b)]) * im[static_cast<size_t> (b)]);
        const double a = std::sqrt (peak) * (f / f0) * (f / f0) * f0 * f0; // undo 1/k^2 in absolute frequency
        // Power per Hz: harmonics are f0 apart, so weight by 1/f0 to compare different pitches.
        num += f * a * a / f0;
        den += a * a / f0;
    }
    return den > 0.0 ? num / den : 0.0;
}

/** Energy in [edges[b], edges[b+1]) Hz, summed over Hann-windowed 16k-point FFT frames. */
template <size_t NumBands>
std::array<double, NumBands> bandEnergies (const std::vector<float>& v, double fs, const std::array<double, NumBands + 1>& edges,
                                           size_t from = 12000)
{
    dsp::Fft fft;
    fft.prepare (14);
    const int n = 1 << 14;
    std::array<double, NumBands> e {};
    std::vector<float> re (n), im (n);
    for (size_t start = from; start + n < v.size(); start += n)
    {
        for (int i = 0; i < n; ++i)
        {
            re[static_cast<size_t> (i)] = v[start + static_cast<size_t> (i)] * (0.5f - 0.5f * std::cos (2.0f * std::numbers::pi_v<float> * i / n));
            im[static_cast<size_t> (i)] = 0.0f;
        }
        fft.perform (re.data(), im.data(), false);
        for (int k = 1; k < n / 2; ++k)
        {
            const double hz = k * fs / n;
            for (size_t b = 0; b < NumBands; ++b)
                if (hz >= edges[b] && hz < edges[b + 1])
                    e[b] += double (re[static_cast<size_t> (k)]) * re[static_cast<size_t> (k)] + double (im[static_cast<size_t> (k)]) * im[static_cast<size_t> (k)];
        }
    }
    return e;
}

} // namespace evox::test
