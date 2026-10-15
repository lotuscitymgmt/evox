#pragma once

#include <vector>

namespace evox::dsp
{

/** In-place iterative radix-2 complex FFT on split real/imag arrays.
    - The first two stages run as one multiply-free radix-4 pass.
    - Every later stage has its own contiguous twiddle table, so the butterfly loop is a
      unit-stride stream the compiler can vectorise.
    Tables are built in prepare(); perform() is allocation-free. */
class Fft
{
public:
    void prepare (int order);
    int getSize() const noexcept { return size_; }

    /** Forward transform (e^-i). Inverse uses e^+i and scales by 1/N. */
    void perform (float* re, float* im, bool inverse) const noexcept;

private:
    int size_ = 0, order_ = 0;
    std::vector<int> bitReverse_;
    std::vector<float> twiddleCos_, twiddleSinForward_, twiddleSinInverse_; // stage tables back to back
    std::vector<int> stageOffset_;
};

} // namespace evox::dsp
