#include "PitchMarker.h"

#include <algorithm>
#include <cmath>

namespace evox::dsp
{

void PitchMarker::prepare (double sampleRate, int maxPeriodSamples)
{
    // Low-pass at 1.2 kHz: keeps the fundamental/low formant peak and ignores the
    // higher-formant ripples that would otherwise make the "biggest peak" jump around.
    smoother_.setCoeffs (BiquadCoeffs::lowpass (sampleRate, 1200.0, 0.70710678));
    unvoicedPeriod_ = static_cast<float> (sampleRate / 150.0);

    std::int64_t size = 1;
    while (size < 4 * maxPeriodSamples)
        size <<= 1;
    history_.assign (static_cast<size_t> (size), 0.0f);
    historyMask_ = size - 1;
    reset();
}

void PitchMarker::reset() noexcept
{
    smoother_.reset();
    std::fill (history_.begin(), history_.end(), 0.0f);
    count_ = lastMark_ = 0;
    lastVoiced_ = false;
}

void PitchMarker::addMark (std::int64_t pos, float period, bool voiced) noexcept
{
    marks_[static_cast<size_t> (count_ & markMask_)] = { pos, period, voiced };
    ++count_;
    lastMark_ = pos;
}

std::int64_t PitchMarker::peakIn (std::int64_t from, std::int64_t to) const noexcept
{
    std::int64_t best = from;
    float bestValue = history_[static_cast<size_t> (from & historyMask_)];
    for (auto i = from + 1; i <= to; ++i)
    {
        const float v = history_[static_cast<size_t> (i & historyMask_)];
        if (v > bestValue)
        {
            bestValue = v;
            best = i;
        }
    }
    return best;
}

void PitchMarker::push (float mono, std::int64_t now, float period, bool voiced) noexcept
{
    history_[static_cast<size_t> (now & historyMask_)] = smoother_.process (mono);

    const auto maxPeriod = static_cast<float> (historyMask_ + 1) / 4.0f;
    period = std::clamp (period, 8.0f, maxPeriod);

    if (count_ == 0)
    {
        addMark (now, unvoicedPeriod_, false);
        return;
    }

    if (voiced)
    {
        if (! lastVoiced_)
        {
            // Onset: wait for one full period of audio, then sync to its strongest peak.
            const auto p = static_cast<std::int64_t> (period);
            if (now - lastMark_ >= p)
            {
                const auto peak = peakIn (now - p + 1, now);
                if (peak > lastMark_)
                {
                    addMark (peak, period, true);
                    lastVoiced_ = true;
                }
            }
            return;
        }

        const auto searchEnd = lastMark_ + static_cast<std::int64_t> (std::ceil (1.3f * period));
        if (now >= searchEnd)
        {
            const auto searchStart = lastMark_ + static_cast<std::int64_t> (0.7f * period);
            addMark (peakIn (std::max (searchStart, lastMark_ + 1), searchEnd), period, true);
        }
    }
    else
    {
        lastVoiced_ = false;
        if (now >= lastMark_ + static_cast<std::int64_t> (unvoicedPeriod_))
            addMark (lastMark_ + static_cast<std::int64_t> (unvoicedPeriod_), unvoicedPeriod_, false);
    }
}

} // namespace evox::dsp
