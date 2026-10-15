#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace evox::dsp
{

/** 32-tap dot product with four independent accumulators (instruction-level parallelism;
    a plain float reduction can't be reordered or vectorised without -ffast-math). */
inline float dot32 (const float* __restrict a, const float* __restrict x, int xStride) noexcept
{
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    for (int k = 0; k < 32; k += 4)
    {
        s0 += a[k] * x[k * xStride];
        s1 += a[k + 1] * x[(k + 1) * xStride];
        s2 += a[k + 2] * x[(k + 2) * xStride];
        s3 += a[k + 3] * x[(k + 3) * xStride];
    }
    return (s0 + s1) + (s2 + s3);
}

/** Linear-phase half-band FIR (63 taps, Kaiser beta 8) for 2x up/down sampling of the
    character (shifter) stage. Odd tap count with the centre at index 31 means every other
    tap is zero, and one polyphase branch of the interpolator is a pure delay.
    Group delay: 31 samples at the 2x rate per stage, so up + down = 31 base samples.
    ~70 dB stopband; passband flat to ~0.42 x base rate (20 kHz at 48 kHz). */
struct HalfbandKernel
{
    static constexpr int taps = 63, centre = 31;
    std::array<float, taps> h {};
    std::array<float, 32> even {}; // h[0], h[2], ... h[62]: the non-zero polyphase branch, contiguous

    HalfbandKernel()
    {
        auto besselI0 = [] (double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };

        constexpr double beta = 8.0;
        const double norm = besselI0 (beta);
        double sum = 0.0;
        for (int n = 0; n < taps; ++n)
        {
            const int m = n - centre;
            const double sinc = m == 0 ? 0.5 : std::sin (std::numbers::pi * 0.5 * m) / (std::numbers::pi * m);
            const double r = double (m) / centre;
            const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / norm;
            h[static_cast<size_t> (n)] = static_cast<float> (sinc * window);
            sum += h[static_cast<size_t> (n)];
        }
        for (auto& c : h)
            c = static_cast<float> (c / sum); // unity DC gain
        for (int k = 0; k < 32; ++k)
            even[static_cast<size_t> (k)] = h[static_cast<size_t> (2 * k)];
    }

    static const HalfbandKernel& get()
    {
        static const HalfbandKernel kernel;
        return kernel;
    }
};

/** 1 base-rate sample in -> 2 samples at 2x. */
class Upsampler2x
{
public:
    void reset() noexcept
    {
        history_.fill (0.0f);
        pos_ = 0;
    }

    void process (float x, float& out0, float& out1) noexcept
    {
        // History of base-rate inputs, newest at pos_ (duplicated for wrap-free reads).
        pos_ = pos_ == 0 ? historySize - 1 : pos_ - 1;
        history_[static_cast<size_t> (pos_)] = history_[static_cast<size_t> (pos_ + historySize)] = x;

        const auto& kernel = HalfbandKernel::get();
        const float* __restrict xs = history_.data() + pos_;
        const float* __restrict e = kernel.even.data();

        // Zero-stuffed stream filtered with gain 2. Output phase 0 uses even taps h[0,2,...,62];
        // phase 1 uses odd taps, which are all zero except the centre (h[31]) -> pure delay.
        out0 = 2.0f * dot32 (e, xs, 1);
        out1 = 2.0f * kernel.h[HalfbandKernel::centre] * xs[(HalfbandKernel::centre - 1) / 2];
    }

private:
    static constexpr int historySize = 32;
    std::array<float, 2 * historySize> history_ {};
    int pos_ = 0;
};

/** 2 samples at 2x in -> 1 base-rate sample out. */
class Downsampler2x
{
public:
    void reset() noexcept
    {
        history_.fill (0.0f);
        pos_ = 0;
    }

    float process (float x0, float x1) noexcept
    {
        // Decimate on the even phase (x0): output m then sits exactly 31 base samples
        // behind, so the wet path stays sample-aligned with the dry path.
        push (x0);
        const auto& kernel = HalfbandKernel::get();
        const float* __restrict xs = history_.data() + pos_; // xs[0] = newest
        const float* __restrict e = kernel.even.data();
        const float acc = kernel.h[HalfbandKernel::centre] * xs[HalfbandKernel::centre] + dot32 (e, xs, 2);
        push (x1);
        return acc;
    }

private:
    void push (float x) noexcept
    {
        pos_ = pos_ == 0 ? historySize - 1 : pos_ - 1;
        history_[static_cast<size_t> (pos_)] = history_[static_cast<size_t> (pos_ + historySize)] = x;
    }

    static constexpr int historySize = 64;
    std::array<float, 2 * historySize> history_ {};
    int pos_ = 0;
};

} // namespace evox::dsp
