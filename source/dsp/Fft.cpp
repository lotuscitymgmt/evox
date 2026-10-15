#include "Fft.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace evox::dsp
{

void Fft::prepare (int order)
{
    size_ = 1 << order;
    bitReverse_.resize (static_cast<size_t> (size_));
    for (int i = 0; i < size_; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            r |= ((i >> b) & 1) << (order - 1 - b);
        bitReverse_[static_cast<size_t> (i)] = r;
    }

    cosTable_.resize (static_cast<size_t> (size_ / 2));
    sinTable_.resize (static_cast<size_t> (size_ / 2));
    for (int k = 0; k < size_ / 2; ++k)
    {
        const auto phase = 2.0 * std::numbers::pi * k / size_;
        cosTable_[static_cast<size_t> (k)] = static_cast<float> (std::cos (phase));
        sinTable_[static_cast<size_t> (k)] = static_cast<float> (std::sin (phase));
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

    const float sign = inverse ? 1.0f : -1.0f;

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1;
        const int tableStep = n / len;

        for (int start = 0; start < n; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const float wr = cosTable_[static_cast<size_t> (k * tableStep)];
                const float wi = sign * sinTable_[static_cast<size_t> (k * tableStep)];
                const int a = start + k, b = a + half;
                const float tr = re[b] * wr - im[b] * wi;
                const float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
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
