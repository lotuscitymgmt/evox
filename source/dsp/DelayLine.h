#pragma once

#include <algorithm>
#include <cassert>
#include <vector>

namespace evox::dsp
{

/** Fixed integer delay. Memory is allocated in prepare(); process() never allocates. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        buffer_.assign (static_cast<size_t> (std::max (1, maxDelaySamples + 1)), 0.0f);
        setDelay (std::min (delay_, maxDelaySamples));
        reset();
    }

    void setDelay (int samples) noexcept
    {
        assert (samples >= 0 && samples < static_cast<int> (buffer_.size()));
        delay_ = samples;
    }

    int getDelay() const noexcept { return delay_; }

    void reset() noexcept
    {
        std::fill (buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }

    float process (float x) noexcept
    {
        const auto size = static_cast<int> (buffer_.size());
        buffer_[static_cast<size_t> (write_)] = x;
        auto read = write_ - delay_;
        if (read < 0)
            read += size;
        if (++write_ == size)
            write_ = 0;
        return buffer_[static_cast<size_t> (read)];
    }

private:
    std::vector<float> buffer_ { 0.0f };
    int write_ = 0, delay_ = 0;
};

} // namespace evox::dsp
