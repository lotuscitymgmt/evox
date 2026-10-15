#pragma once

#include <cmath>
#include <numbers>

namespace evox::dsp
{

/** RBJ-cookbook biquad coefficients (normalised so a0 == 1). */
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static BiquadCoeffs highpass (double fs, double fc, double q) noexcept
    {
        const auto w0 = 2.0 * std::numbers::pi * fc / fs;
        const auto cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const auto a0 = 1.0 + alpha;
        return { (1.0 + cw) * 0.5 / a0, -(1.0 + cw) / a0, (1.0 + cw) * 0.5 / a0,
                 -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    /** RBJ high shelf, shelf slope S = 1. */
    static BiquadCoeffs highShelf (double fs, double fc, double gainDb) noexcept
    {
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w0 = 2.0 * std::numbers::pi * fc / fs;
        const auto cw = std::cos (w0), alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
        const auto sa = 2.0 * std::sqrt (a) * alpha;
        const auto a0 = (a + 1.0) - (a - 1.0) * cw + sa;
        return { a * ((a + 1.0) + (a - 1.0) * cw + sa) / a0,
                 -2.0 * a * ((a - 1.0) + (a + 1.0) * cw) / a0,
                 a * ((a + 1.0) + (a - 1.0) * cw - sa) / a0,
                 2.0 * ((a - 1.0) - (a + 1.0) * cw) / a0,
                 ((a + 1.0) - (a - 1.0) * cw - sa) / a0 };
    }

    static BiquadCoeffs lowpass (double fs, double fc, double q) noexcept
    {
        const auto w0 = 2.0 * std::numbers::pi * fc / fs;
        const auto cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const auto a0 = 1.0 + alpha;
        return { (1.0 - cw) * 0.5 / a0, (1.0 - cw) / a0, (1.0 - cw) * 0.5 / a0,
                 -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }
};

/** Transposed direct form II biquad. Double-precision state keeps low cut-offs stable. */
class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) noexcept { c_ = c; }
    void reset() noexcept { z1_ = z2_ = 0.0; }

    float process (float xIn) noexcept
    {
        const double x = xIn;
        const double y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y + z2_;
        z2_ = c_.b2 * x - c_.a2 * y;
        return static_cast<float> (y);
    }

private:
    BiquadCoeffs c_;
    double z1_ = 0.0, z2_ = 0.0;
};

/** First-order DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]. */
class DcBlocker
{
public:
    void prepare (double fs, double cutoffHz = 10.0) noexcept
    {
        r_ = std::exp (-2.0 * std::numbers::pi * cutoffHz / fs);
        reset();
    }

    void reset() noexcept { x1_ = y1_ = 0.0; }

    float process (float xIn) noexcept
    {
        const double x = xIn;
        const double y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return static_cast<float> (y);
    }

private:
    double r_ = 0.999, x1_ = 0.0, y1_ = 0.0;
};

} // namespace evox::dsp
