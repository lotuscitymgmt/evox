#include "YinPitchDetector.h"

#include <algorithm>
#include <cmath>

namespace evox::dsp
{

namespace
{
int orderFor (int length) noexcept
{
    int order = 0;
    while ((1 << order) < length)
        ++order;
    return order;
}
} // namespace

void YinPitchDetector::prepare (double analysisRate, float lowestHz)
{
    rate_ = analysisRate;
    maxTauAllocated_ = static_cast<int> (std::ceil (analysisRate / lowestHz)) + 2;

    // Window == tauMax in the worst case, plus one guard sample for interpolation.
    const int maxFrame = 2 * maxTauAllocated_ + 2;
    const int maxFftOrder = std::clamp (orderFor (maxFrame), minOrder, maxOrder);

    for (int order = minOrder; order <= maxFftOrder; ++order)
        ffts_[static_cast<size_t> (order)].prepare (order);

    const auto n = static_cast<size_t> (1 << maxFftOrder);
    re_.assign (n, 0.0f);
    im_.assign (n, 0.0f);
    re2_.assign (n, 0.0f);
    im2_.assign (n, 0.0f);
    cmndf_.assign (static_cast<size_t> (maxTauAllocated_ + 2), 1.0f);
    diff_.assign (static_cast<size_t> (maxTauAllocated_ + 2), 0.0f);
    energy_.assign (static_cast<size_t> (maxFrame + 1), 0.0);

    configure (80.0f, 800.0f);
}

void YinPitchDetector::configure (float minHz, float maxHz) noexcept
{
    tauMax_ = std::min (static_cast<int> (std::ceil (rate_ / minHz)), maxTauAllocated_ - 1);
    tauMin_ = std::max (2, static_cast<int> (std::floor (rate_ / maxHz)));

    // Integration window equal to the longest period: long enough to be robust, short
    // enough to keep tracking tight. High voices therefore get a shorter window.
    window_ = std::max (tauMax_, 64);
    fftOrder_ = std::clamp (orderFor (getFrameLength()), minOrder, maxOrder);
}

YinPitchDetector::Result YinPitchDetector::analyse (const float* frame, float threshold) noexcept
{
    const int n = getFrameLength();
    const int m = 1 << fftOrder_;
    const int w = window_;

    // Energy prefix sums: energy_[k] = sum_{j<k} x_j^2
    energy_[0] = 0.0;
    for (int i = 0; i < n; ++i)
        energy_[static_cast<size_t> (i + 1)] = energy_[static_cast<size_t> (i)] + double (frame[i]) * frame[i];

    const double e1 = energy_[static_cast<size_t> (w)];
    if (e1 < 1.0e-10 * w)
    {
        std::fill (cmndf_.begin(), cmndf_.end(), 1.0f);
        return {};
    }

    // Pack a = first W samples (real) and b = whole frame (imag) into one FFT.
    for (int i = 0; i < m; ++i)
    {
        re_[static_cast<size_t> (i)] = i < w ? frame[i] : 0.0f;
        im_[static_cast<size_t> (i)] = i < n ? frame[i] : 0.0f;
    }

    const auto& fft = ffts_[static_cast<size_t> (fftOrder_)];
    fft.perform (re_.data(), im_.data(), false);

    // Unpack A and B, form conj(A) * B (cross-correlation spectrum).
    for (int k = 0; k < m; ++k)
    {
        const int mk = (m - k) & (m - 1);
        const float zr = re_[static_cast<size_t> (k)], zi = im_[static_cast<size_t> (k)];
        const float mr = re_[static_cast<size_t> (mk)], mi = im_[static_cast<size_t> (mk)];
        const float ar = 0.5f * (zr + mr), ai = 0.5f * (zi - mi);
        const float br = 0.5f * (zi + mi), bi = -0.5f * (zr - mr);
        re2_[static_cast<size_t> (k)] = ar * br + ai * bi;
        im2_[static_cast<size_t> (k)] = ar * bi - ai * br;
    }

    fft.perform (re2_.data(), im2_.data(), true);

    // d(tau) = E(0..W) + E(tau..tau+W) - 2 r(tau), then cumulative mean normalisation.
    cmndf_[0] = 1.0f;
    diff_[0] = 0.0f;
    double runningSum = 0.0;
    for (int tau = 1; tau <= tauMax_ + 1; ++tau)
    {
        const double e2 = energy_[static_cast<size_t> (tau + w)] - energy_[static_cast<size_t> (tau)];
        const double d = std::max (0.0, e1 + e2 - 2.0 * double (re2_[static_cast<size_t> (tau)]));
        diff_[static_cast<size_t> (tau)] = static_cast<float> (d);
        runningSum += d;
        cmndf_[static_cast<size_t> (tau)] = runningSum > 0.0 ? static_cast<float> (d * tau / runningSum) : 1.0f;
    }

    // Absolute threshold: first dip below threshold, then slide down to its local minimum.
    for (int tau = tauMin_; tau <= tauMax_; ++tau)
    {
        if (cmndf_[static_cast<size_t> (tau)] < threshold)
        {
            while (tau + 1 <= tauMax_ && cmndf_[static_cast<size_t> (tau + 1)] < cmndf_[static_cast<size_t> (tau)])
                ++tau;
            return preferSubharmonic (makeResult (tau, true));
        }
    }

    // No dip under threshold: report the global minimum as a low-confidence estimate.
    int best = tauMin_;
    for (int tau = tauMin_ + 1; tau <= tauMax_; ++tau)
        if (cmndf_[static_cast<size_t> (tau)] < cmndf_[static_cast<size_t> (best)])
            best = tau;

    return makeResult (best, false);
}

YinPitchDetector::Result YinPitchDetector::bestDipNear (float periodSamples, float relTolerance) const noexcept
{
    const int lo = std::max (tauMin_, static_cast<int> (std::floor (periodSamples * (1.0f - relTolerance))));
    const int hi = std::min (tauMax_, static_cast<int> (std::ceil (periodSamples * (1.0f + relTolerance))));

    if (lo > hi)
        return {};

    int best = lo;
    for (int tau = lo + 1; tau <= hi; ++tau)
        if (cmndf_[static_cast<size_t> (tau)] < cmndf_[static_cast<size_t> (best)])
            best = tau;

    return makeResult (best, false);
}

YinPitchDetector::Result YinPitchDetector::preferSubharmonic (Result r) const noexcept
{
    // YIN's first-dip rule picks the shortest acceptable period, which yields octave-up
    // (or twelfth-up) errors when the fundamental is weak relative to its 2nd/3rd
    // harmonic. If a dip at 2x or 3x the period is *much* deeper, that is the true period.
    // A genuinely periodic signal has equally deep dips at every multiple, so it is unaffected.
    for (int multiple : { 2, 3 })
    {
        if (r.aperiodicity < 0.03f || r.periodSamples * static_cast<float> (multiple) > static_cast<float> (tauMax_))
            continue;

        const auto alt = bestDipNear (r.periodSamples * static_cast<float> (multiple), 0.03f);
        if (alt.periodSamples > 0.0f && alt.aperiodicity < 0.4f * r.aperiodicity)
        {
            auto chosen = alt;
            chosen.belowThreshold = true;
            return chosen;
        }
    }
    return r;
}

float YinPitchDetector::interpolate (int tau, float& valueOut) const noexcept
{
    const float s1 = cmndf_[static_cast<size_t> (tau)];
    valueOut = s1;

    if (tau < 1 || tau > tauMax_)
        return static_cast<float> (tau);

    // Vertex of the parabola through the *raw* difference function: the cumulative-mean
    // normalisation skews d'(tau) at short lags and biases high-f0 estimates sharp.
    const float d0 = diff_[static_cast<size_t> (tau - 1)];
    const float d1 = diff_[static_cast<size_t> (tau)];
    const float d2 = diff_[static_cast<size_t> (tau + 1)];
    const float denom = d0 - 2.0f * d1 + d2;

    if (denom <= 1.0e-20f)
        return static_cast<float> (tau);

    const float delta = std::clamp (0.5f * (d0 - d2) / denom, -1.0f, 1.0f);

    // Report d' at the interpolated position too: integer-lag values are pessimistic for
    // short periods, which would skew comparisons between dips at different multiples.
    const float c0 = cmndf_[static_cast<size_t> (tau - 1)], c2 = cmndf_[static_cast<size_t> (tau + 1)];
    valueOut = std::clamp (s1 + 0.5f * delta * (c2 - c0) + 0.5f * delta * delta * (c0 - 2.0f * s1 + c2), 0.0f, s1);
    return static_cast<float> (tau) + delta;
}

YinPitchDetector::Result YinPitchDetector::makeResult (int tau, bool below) const noexcept
{
    Result r;
    r.periodSamples = interpolate (tau, r.aperiodicity);
    r.f0Hz = static_cast<float> (rate_ / r.periodSamples);
    r.belowThreshold = below;
    return r;
}

} // namespace evox::dsp
