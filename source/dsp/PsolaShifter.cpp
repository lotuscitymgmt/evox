#include "PsolaShifter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evox::dsp
{

namespace
{
std::int64_t powerOfTwoAtLeast (double n)
{
    std::int64_t size = 1;
    while (static_cast<double> (size) < n)
        size <<= 1;
    return size;
}
} // namespace

void PsolaShifter::prepare (double sampleRate, int latencySamples, float lowestHz)
{
    sampleRate_ = sampleRate;
    latency_ = latencySamples;
    maxPeriod_ = static_cast<float> (sampleRate / lowestHz);

    marker_.prepare (sampleRate, static_cast<int> (std::ceil (maxPeriod_)));

    // Input history: a grain compressed 4x reads +/-4 periods of source, and may sit a few
    // periods behind the output point.
    const auto inputSize = powerOfTwoAtLeast (16.0 * maxPeriod_ + latencySamples + 16);
    // Accumulator: grains span at most 2 periods ahead of p.
    const auto accumSize = powerOfTwoAtLeast (8.0 * maxPeriod_ + 16);

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        input_[static_cast<size_t> (ch)].assign (static_cast<size_t> (inputSize), 0.0f);
        accum_[static_cast<size_t> (ch)].assign (static_cast<size_t> (accumSize), 0.0f);
    }
    inputMask_ = inputSize - 1;
    accumMask_ = accumSize - 1;

    appliedCharacter_ = -1.0f;
    setSettings (settings_);
    reset();
}

void PsolaShifter::reset() noexcept
{
    marker_.reset();
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        std::fill (input_[static_cast<size_t> (ch)].begin(), input_[static_cast<size_t> (ch)].end(), 0.0f);
        std::fill (accum_[static_cast<size_t> (ch)].begin(), accum_[static_cast<size_t> (ch)].end(), 0.0f);
        presence_[static_cast<size_t> (ch)].reset();
    }
    cursor_ = {};
    nextSynthesis_ = lastSynthesis_ = 0.0;
    now_ = markSearch_ = grains_ = 0;
    started_ = false;
}

void PsolaShifter::restartSynthesis() noexcept
{
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        std::fill (accum_[static_cast<size_t> (ch)].begin(), accum_[static_cast<size_t> (ch)].end(), 0.0f);
        presence_[static_cast<size_t> (ch)].reset();
    }
    cursor_ = {};
    markSearch_ = 0;
    started_ = false;
}

void PsolaShifter::setSettings (const Settings& s) noexcept
{
    settings_ = s;
    settings_.throat = std::clamp (s.throat, 0.5f, 1.8f);
    const float character = std::clamp (s.character, 0.0f, 1.0f);
    if (std::abs (character - appliedCharacter_) > 1.0e-4f)
        rebuildWindow (character);
}

void PsolaShifter::rebuildWindow (float character) noexcept
{
    appliedCharacter_ = character;
    for (auto& shelf : presence_)
        shelf.setCoeffs (BiquadCoeffs::highShelf (sampleRate_, 3500.0, 5.0 * character));
    halfFactor_ = 1.0f - 0.35f * character;
    const float exponent = 1.0f + 2.0f * character;

    double area = 0.0;
    for (int i = 0; i <= windowSize; ++i)
    {
        const double x = -1.0 + 2.0 * i / windowSize;
        const double hann = 0.5 * (1.0 + std::cos (std::numbers::pi * x));
        const auto w = static_cast<float> (std::pow (hann, double (exponent)));
        windowTable_[static_cast<size_t> (i)] = w;
        area += (i == 0 || i == windowSize ? 0.5 : 1.0) * w;
    }
    windowTable_[windowSize + 1] = 0.0f;
    windowArea_ = static_cast<float> (area * 2.0 / windowSize); // integral over [-1, 1]
}

float PsolaShifter::window (float x) const noexcept
{
    const float pos = (x + 1.0f) * 0.5f * windowSize;
    if (pos <= 0.0f || pos >= static_cast<float> (windowSize))
        return 0.0f;
    const auto i = static_cast<int> (pos);
    const float frac = pos - static_cast<float> (i);
    return windowTable_[static_cast<size_t> (i)] + frac * (windowTable_[static_cast<size_t> (i + 1)] - windowTable_[static_cast<size_t> (i)]);
}

float PsolaShifter::readHermite (int channel, double position) const noexcept
{
    const auto& buf = input_[static_cast<size_t> (channel)];
    const auto i = static_cast<std::int64_t> (std::floor (position));
    const auto t = static_cast<float> (position - static_cast<double> (i));
    const float y0 = buf[static_cast<size_t> ((i - 1) & inputMask_)];
    const float y1 = buf[static_cast<size_t> (i & inputMask_)];
    const float y2 = buf[static_cast<size_t> ((i + 1) & inputMask_)];
    const float y3 = buf[static_cast<size_t> ((i + 2) & inputMask_)];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void PsolaShifter::pushInput (const float* channels, int numChannels, float mono, std::int64_t now,
                              float detectedPeriod, bool voiced) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
        input_[static_cast<size_t> (ch)][static_cast<size_t> (now & inputMask_)] = channels[ch];

    now_ = now;
    marker_.push (mono, now, detectedPeriod, voiced);
}

float PsolaShifter::grainStretch (float ratio) const noexcept
{
    const float alpha = settings_.formantCorrection ? settings_.throat : settings_.throat / ratio;
    return std::clamp (alpha, 0.25f, 4.0f);
}

float PsolaShifter::outputHalfLength (float period, float alpha, float ratio) const noexcept
{
    // Output grain half-length, in units of halfFactor x input period:
    //  - up to the output spacing (1 / ratio when lowering), so lowered pitches don't open
    //    gaps between grains...
    //  - ...but never reading more than ~one source period per side unless the grain is
    //    being compressed (alpha < 1), or it would carry two glottal pulses into one grain.
    // Cases: formant on / lowering -> 1 (classic PSOLA); formant off / -12 st -> 2 (full
    // period stretched x2); throat > 100 % -> 1 (stretched grains read less source, no comb
    // filtering); throat < 100 % -> 1 (compressed grains read more source, no smearing).
    const float spacingLimit = std::max (1.0f, 1.0f / ratio);
    const float sourceLimit = std::max (1.0f, alpha);
    return halfFactor_ * period * std::min (spacingLimit, sourceLimit);
}

void PsolaShifter::placeGrain (double ts, float period, float ratio, std::int64_t p, int numChannels) noexcept
{
    const float alpha = grainStretch (ratio);
    const float halfOut = outputHalfLength (period, alpha, ratio); // grain half-length in the output
    const float halfSource = halfOut / alpha;                       // ...and in the input it reads

    // Choose the analysis mark nearest to t_s among those whose grain is fully available.
    const auto available = static_cast<double> (now_) - 2.0; // Hermite needs i + 2
    const auto first = marker_.getOldestMarkIndex();
    const auto count = marker_.getNumMarks();
    if (count == 0)
        return;

    std::int64_t best = -1;
    double bestDistance = 1.0e300;
    for (auto m = std::max (first, markSearch_); m < count; ++m)
    {
        const auto& mark = marker_.getMark (m);
        if (static_cast<double> (mark.position) + halfSource > available)
            break;
        const double distance = std::abs (static_cast<double> (mark.position) - ts);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = m;
        }
    }

    if (best < 0)
        return;

    // Marks earlier than the chosen one minus a few can never be nearest again.
    markSearch_ = std::max (first, best - 4);

    const double ta = static_cast<double> (marker_.getMark (best).position);

    // Unity overlap-add gain for this grain density: T_out / (window area in samples).
    const float spacing = period / ratio;
    const float gain = spacing / (halfOut * windowArea_);

    const auto start = std::max (p, static_cast<std::int64_t> (std::ceil (ts - halfOut)));
    const auto end = static_cast<std::int64_t> (std::floor (ts + halfOut));
    const float invHalfOut = 1.0f / halfOut;
    const double invAlpha = 1.0 / alpha;

    for (auto n = start; n <= end; ++n)
    {
        const double offset = static_cast<double> (n) - ts;
        const float w = gain * window (static_cast<float> (offset) * invHalfOut);
        if (w == 0.0f)
            continue;
        const double src = ta + offset * invAlpha;
        for (int ch = 0; ch < numChannels; ++ch)
            accum_[static_cast<size_t> (ch)][static_cast<size_t> (n & accumMask_)] += w * readHermite (ch, src);
    }

    ++grains_;
    lastSynthesis_ = ts;
}

void PsolaShifter::render (std::int64_t p, const CorrectionTrack& track, float* out, int numChannels) noexcept
{
    if (! started_)
    {
        nextSynthesis_ = static_cast<double> (p);
        started_ = true;
    }

    // Place every grain whose left edge has reached the output point.
    for (int guard = 0; guard < 64; ++guard)
    {
        const auto trackIndex = static_cast<std::int64_t> (std::floor ((nextSynthesis_ - trackOffset_) / trackScale_));
        const auto decision = track.at (cursor_, trackIndex);
        const bool voiced = decision.voiced && decision.periodSamples > 0.0f;
        const float period = voiced ? std::clamp (decision.periodSamples * static_cast<float> (trackScale_), 8.0f, maxPeriod_)
                                    : marker_.getUnvoicedPeriod();
        const float ratio = voiced ? std::clamp (decision.ratio, 0.25f, 4.0f) : 1.0f;

        const float alpha = grainStretch (ratio);
        const double lead = outputHalfLength (period, alpha, ratio);

        if (nextSynthesis_ - lead > static_cast<double> (p))
            break;

        placeGrain (nextSynthesis_, period, ratio, p, numChannels);
        nextSynthesis_ += std::max (4.0, static_cast<double> (period / ratio));
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& slot = accum_[static_cast<size_t> (ch)][static_cast<size_t> (p & accumMask_)];
        out[ch] = presence_[static_cast<size_t> (ch)].process (slot);
        slot = 0.0f;
    }
}

} // namespace evox::dsp
