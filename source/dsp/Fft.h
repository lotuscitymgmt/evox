#pragma once

#include <vector>

namespace evox::dsp
{

/** In-place iterative radix-2 complex FFT on split real/imag arrays.
    Tables are built in prepare(); perform() is allocation-free. */
class Fft
{
public:
    void prepare (int order);
    int getSize() const noexcept { return size_; }

    /** Forward transform (e^-i). Inverse uses e^+i and scales by 1/N. */
    void perform (float* re, float* im, bool inverse) const noexcept;

private:
    int size_ = 0;
    std::vector<int> bitReverse_;
    std::vector<float> cosTable_, sinTable_;
};

} // namespace evox::dsp
