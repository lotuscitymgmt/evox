#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace evox::dsp
{

/** Timestamped pitch decisions, so the shifter applies each decision to the exact audio
    it was measured on rather than to whatever happens to be at the synthesis point.
    Single-thread (audio thread) ring with independent read cursors. */
class CorrectionTrack
{
public:
    struct Entry
    {
        std::int64_t sample = 0;     // input sample index the decision describes
        float periodSamples = 0.0f;  // input period at host rate (0 = unvoiced)
        float ratio = 1.0f;          // output f0 / input f0
        bool voiced = false;
    };

    struct Cursor
    {
        std::int64_t index = -1;
    };

    void reset() noexcept
    {
        written_ = 0;
        lastSample_ = INT64_MIN;
    }

    void push (Entry e) noexcept
    {
        // Window-centre timestamps depend on the period, so keep them monotonic.
        if (e.sample <= lastSample_)
            e.sample = lastSample_ + 1;
        lastSample_ = e.sample;
        entries_[static_cast<std::size_t> (written_ & mask)] = e;
        ++written_;
    }

    /** The latest decision at or before `sample` (or the newest one if all are older). */
    Entry at (Cursor& c, std::int64_t sample) const noexcept
    {
        if (written_ == 0)
            return {};

        if (c.index < written_ - capacity + 1)
            c.index = written_ - capacity + 1;
        if (c.index < 0)
            c.index = 0;

        while (c.index + 1 < written_ && entries_[static_cast<std::size_t> ((c.index + 1) & mask)].sample <= sample)
            ++c.index;

        return entries_[static_cast<std::size_t> (c.index & mask)];
    }

private:
    static constexpr std::int64_t capacity = 512, mask = capacity - 1;
    std::array<Entry, capacity> entries_ {};
    std::int64_t written_ = 0, lastSample_ = INT64_MIN;
};

} // namespace evox::dsp
