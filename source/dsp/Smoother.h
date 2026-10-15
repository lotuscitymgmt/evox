#pragma once

#include <algorithm>
#include <cmath>

namespace evox::dsp
{

/** Linear-ramp parameter smoother (JUCE-free so the core stays host-agnostic). */
class LinearSmoother
{
public:
    void prepare (double fs, double rampSeconds) noexcept
    {
        rampLength_ = static_cast<int> (std::max (1.0, fs * rampSeconds));
        setImmediate (target_);
    }

    void setImmediate (float v) noexcept
    {
        current_ = target_ = v;
        remaining_ = 0;
    }

    void setTarget (float v) noexcept
    {
        if (std::abs (v - target_) <= 1.0e-9f)
            return;
        target_ = v;
        remaining_ = rampLength_;
        step_ = (target_ - current_) / static_cast<float> (rampLength_);
    }

    float next() noexcept
    {
        if (remaining_ > 0)
        {
            current_ += step_;
            if (--remaining_ == 0)
                current_ = target_;
        }
        return current_;
    }

    float getTarget() const noexcept { return target_; }
    bool isSmoothing() const noexcept { return remaining_ > 0; }

private:
    float current_ = 0.0f, target_ = 0.0f, step_ = 0.0f;
    int rampLength_ = 1, remaining_ = 0;
};

} // namespace evox::dsp
