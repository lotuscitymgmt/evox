#pragma once

#include "Biquad.h"

#include <cstdint>
#include <vector>

namespace evox::dsp
{

/** Stage 4a: places pitch marks on glottal-period peaks of the (undelayed) mono input.

    Each new mark is the largest positive peak of a low-passed copy of the signal inside
    [last + 0.7 T, last + 1.3 T]. Always choosing the same polarity of the same smoothed
    peak keeps marks one period apart and phase-coherent from cycle to cycle, which is
    what lets re-placed grains add up without flamming. During unvoiced audio marks are
    laid on a fixed grid; at each voiced onset the grid re-syncs to the strongest peak. */
class PitchMarker
{
public:
    struct Mark
    {
        std::int64_t position = 0;
        float period = 0.0f;
        bool voiced = false;
    };

    void prepare (double sampleRate, int maxPeriodSamples);
    void reset() noexcept;

    /** Feed one sample at absolute index `now`. `period` is the latest detected period in
        host samples (ignored when unvoiced). */
    void push (float mono, std::int64_t now, float period, bool voiced) noexcept;

    std::int64_t getNumMarks() const noexcept { return count_; }
    const Mark& getMark (std::int64_t index) const noexcept { return marks_[static_cast<size_t> (index & markMask_)]; }
    std::int64_t getOldestMarkIndex() const noexcept { return count_ > markCapacity ? count_ - markCapacity : 0; }

    float getUnvoicedPeriod() const noexcept { return unvoicedPeriod_; }

private:
    void addMark (std::int64_t pos, float period, bool voiced) noexcept;
    std::int64_t peakIn (std::int64_t from, std::int64_t to) const noexcept;

    static constexpr std::int64_t markCapacity = 1024, markMask_ = markCapacity - 1;

    Biquad smoother_;
    std::vector<float> history_;
    std::int64_t historyMask_ = 0;
    std::vector<Mark> marks_ = std::vector<Mark> (static_cast<size_t> (markCapacity));
    std::int64_t count_ = 0, lastMark_ = 0;
    float unvoicedPeriod_ = 320.0f;
    bool lastVoiced_ = false;
};

} // namespace evox::dsp
