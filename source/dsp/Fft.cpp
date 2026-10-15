#include "Fft.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace evox::dsp
{

namespace
{
// Restrict-qualified parameters let the compiler vectorise this unit-stride butterfly run.
void butterflies (float* __restrict ar, float* __restrict ai, float* __restrict br, float* __restrict bi,
                  const float* __restrict wr, const float* __restrict wi, int half) noexcept
{
    for (int k = 0; k < half; ++k)
    {
        const float tr = br[k] * wr[k] - bi[k] * wi[k];
        const float ti = br[k] * wi[k] + bi[k] * wr[k];
        br[k] = ar[k] - tr;
        bi[k] = ai[k] - ti;
        ar[k] += tr;
        ai[k] += ti;
    }
}
} // namespace

void Fft::prepare (int order)
{
    order_ = order;
    size_ = 1 << order;
    bitReverse_.resize (static_cast<size_t> (size_));
    for (int i = 0; i < size_; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            r |= ((i >> b) & 1) << (order - 1 - b);
        bitReverse_[static_cast<size_t> (i)] = r;
    }

    // Twiddles for stages with butterfly span len >= 8 (len 2 and 4 are multiply-free).
    twiddleCos_.clear();
    twiddleSinForward_.clear();
    twiddleSinInverse_.clear();
    stageOffset_.assign (static_cast<size_t> (order + 1), 0);
    for (int s = 3; s <= order; ++s)
    {
        const int len = 1 << s, half = len >> 1;
        stageOffset_[static_cast<size_t> (s)] = static_cast<int> (twiddleCos_.size());
        for (int k = 0; k < half; ++k)
        {
            const auto phase = 2.0 * std::numbers::pi * k / len;
            twiddleCos_.push_back (static_cast<float> (std::cos (phase)));
            twiddleSinForward_.push_back (static_cast<float> (-std::sin (phase)));
            twiddleSinInverse_.push_back (static_cast<float> (std::sin (phase)));
        }
    }
}

void Fft::perform (float* re, float* im, bool inverse) const noexcept
{
    const int n = size_;

    for (int i = 0; i < n; ++i)
    {
        const int j = bitReverse_[static_cast<size_t> (i)];
        if (j > i)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }

    // Stages 1+2 as radix-4 (twiddles are 1 and -/+ i only).
    if (n >= 4)
    {
        const float sign = inverse ? 1.0f : -1.0f;
        for (int i = 0; i < n; i += 4)
        {
            const float ar = re[i] + re[i + 1], ai = im[i] + im[i + 1];
            const float br = re[i] - re[i + 1], bi = im[i] - im[i + 1];
            const float cr = re[i + 2] + re[i + 3], ci = im[i + 2] + im[i + 3];
            const float dr = re[i + 2] - re[i + 3], di = im[i + 2] - im[i + 3];
            // d * (+/- i)
            const float tr = -sign * di, ti = sign * dr;
            re[i] = ar + cr;      im[i] = ai + ci;
            re[i + 2] = ar - cr;  im[i + 2] = ai - ci;
            re[i + 1] = br + tr;  im[i + 1] = bi + ti;
            re[i + 3] = br - tr;  im[i + 3] = bi - ti;
        }
    }
    else if (n == 2)
    {
        const float r0 = re[0], i0 = im[0];
        re[0] = r0 + re[1]; im[0] = i0 + im[1];
        re[1] = r0 - re[1]; im[1] = i0 - im[1];
    }

    const auto& sinTable = inverse ? twiddleSinInverse_ : twiddleSinForward_;
    for (int s = 3; s <= order_; ++s)
    {
        const int len = 1 << s, half = len >> 1;
        const auto offset = static_cast<size_t> (stageOffset_[static_cast<size_t> (s)]);
        for (int start = 0; start < n; start += len)
            butterflies (re + start, im + start, re + start + half, im + start + half,
                         twiddleCos_.data() + offset, sinTable.data() + offset, half);
    }

    if (inverse)
    {
        const float scale = 1.0f / static_cast<float> (n);
        for (int i = 0; i < n; ++i)
        {
            re[i] *= scale;
            im[i] *= scale;
        }
    }
}

} // namespace evox::dsp
