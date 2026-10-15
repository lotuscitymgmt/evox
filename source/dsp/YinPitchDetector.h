#pragma once

#include "Fft.h"

#include <array>
#include <vector>

namespace evox::dsp
{

/** Stage 2 core: YIN fundamental-frequency estimator (de Cheveigné & Kawahara, 2002).

    - The difference function d(tau) is computed in O(N log N) via one complex FFT
      (both correlation operands packed into real/imag) and energy prefix sums.
    - Cumulative-mean-normalised difference d'(tau), absolute threshold, then
      parabolic interpolation around the chosen dip for sub-sample period accuracy.

    Frame layout: the caller passes getFrameLength() samples, NEWEST first. The
    integration window is the first W samples, so the estimate describes the most recent
    W + period samples rather than the oldest - this is what keeps tracking lag low. */
class YinPitchDetector
{
public:
    struct Result
    {
        float periodSamples = 0.0f;  // interpolated period at the analysis rate
        float f0Hz = 0.0f;
        float aperiodicity = 1.0f;   // d'(tau) at the chosen dip, 0 = perfectly periodic
        bool belowThreshold = false; // true when a dip crossed the absolute threshold
    };

    /** Allocates for the widest range that will ever be configured. */
    void prepare (double analysisRate, float lowestHz);

    /** Selects the search range; no allocation, safe on the audio thread. */
    void configure (float minHz, float maxHz) noexcept;

    int getFrameLength() const noexcept { return window_ + tauMax_ + 1; }
    int getWindowLength() const noexcept { return window_; }
    int getTauMin() const noexcept { return tauMin_; }
    int getTauMax() const noexcept { return tauMax_; }
    double getAnalysisRate() const noexcept { return rate_; }

    Result analyse (const float* frame, float threshold) noexcept;

    /** After analyse(): the best dip within +/- relTolerance of a given period.
        Used by the tracker to test whether an octave jump is genuine. */
    Result bestDipNear (float periodSamples, float relTolerance) const noexcept;

    /** d'(tau) from the last analyse(), valid for tau in [0, tauMax + 1]. */
    const float* getCmndf() const noexcept { return cmndf_.data(); }

private:
    float interpolate (int tau, float& valueOut) const noexcept;
    Result makeResult (int tau, bool below) const noexcept;
    Result preferSubharmonic (Result r) const noexcept;

    static constexpr int minOrder = 6, maxOrder = 13;
    std::array<Fft, maxOrder + 1> ffts_;

    double rate_ = 24000.0;
    int tauMin_ = 2, tauMax_ = 300, window_ = 300, maxTauAllocated_ = 0;
    int fftOrder_ = minOrder;

    std::vector<float> re_, im_, re2_, im2_, cmndf_, diff_;
    std::vector<double> energy_;
};

} // namespace evox::dsp
