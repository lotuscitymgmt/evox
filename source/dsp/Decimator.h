#pragma once

#include "Biquad.h"

namespace evox::dsp
{

/** Anti-aliased integer decimator for the analysis path only (never heard).
    Picks the largest power-of-two factor that keeps the analysis rate >= 20 kHz,
    so 44.1/48 kHz -> /2, 88.2/96 kHz -> /4, 176.4/192 kHz -> /8. */
class Decimator
{
public:
    static int factorFor (double sampleRate) noexcept
    {
        int factor = 1;
        while (sampleRate / (factor * 2) >= 20000.0)
            factor *= 2;
        return factor;
    }

    void prepare (double sampleRate)
    {
        factor_ = factorFor (sampleRate);
        // 6th-order Butterworth: aliasing folds inharmonic energy into the analysis band,
        // which raises YIN's aperiodicity and biases high-f0 estimates.
        const double cutoff = 0.36 * sampleRate / factor_;
        lp1_.setCoeffs (BiquadCoeffs::lowpass (sampleRate, cutoff, 0.51763809));
        lp2_.setCoeffs (BiquadCoeffs::lowpass (sampleRate, cutoff, 0.70710678));
        lp3_.setCoeffs (BiquadCoeffs::lowpass (sampleRate, cutoff, 1.93185165));
        reset();
    }

    void reset() noexcept
    {
        lp1_.reset();
        lp2_.reset();
        lp3_.reset();
        phase_ = 0;
    }

    int getFactor() const noexcept { return factor_; }

    /** Returns true and writes `out` once every `factor` input samples. */
    bool process (float x, float& out) noexcept
    {
        const float y = factor_ > 1 ? lp3_.process (lp2_.process (lp1_.process (x))) : x;
        if (++phase_ < factor_)
            return false;
        phase_ = 0;
        out = y;
        return true;
    }

private:
    Biquad lp1_, lp2_, lp3_;
    int factor_ = 1, phase_ = 0;
};

} // namespace evox::dsp
