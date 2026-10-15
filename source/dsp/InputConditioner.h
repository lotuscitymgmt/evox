#pragma once

#include "Biquad.h"

namespace evox::dsp
{

/** Stage 1a: per-channel DC blocker followed by an optional 60 Hz high-pass
    (2nd-order Butterworth, 12 dB/oct) that removes rumble and plosive thumps
    before they can confuse the pitch detector. */
class InputConditioner
{
public:
    static constexpr double lowCutHz = 60.0;

    void prepare (double sampleRate)
    {
        dc_.prepare (sampleRate, 5.0);
        hpf_.setCoeffs (BiquadCoeffs::highpass (sampleRate, lowCutHz, 0.70710678));
        reset();
    }

    void reset() noexcept
    {
        dc_.reset();
        hpf_.reset();
    }

    void setLowCutEnabled (bool shouldBeEnabled) noexcept
    {
        if (shouldBeEnabled != lowCut_)
        {
            lowCut_ = shouldBeEnabled;
            hpf_.reset();
        }
    }

    float process (float x) noexcept
    {
        auto y = dc_.process (x);
        return lowCut_ ? hpf_.process (y) : y;
    }

private:
    DcBlocker dc_;
    Biquad hpf_;
    bool lowCut_ = true;
};

} // namespace evox::dsp
