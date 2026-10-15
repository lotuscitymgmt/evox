#include "PitchAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace evox::dsp
{

void PitchAnalyzer::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    decimator_.prepare (sampleRate);
    const double analysisRate = sampleRate / decimator_.getFactor();

    float lowest = 1.0e6f;
    for (auto t : { InputType::soprano, InputType::altoTenor, InputType::lowMale, InputType::instrument })
        lowest = std::min (lowest, pitchRangeFor (t).minHz);

    yin_.prepare (analysisRate, lowest);
    yin_.configure (lowest, 2000.0f);

    int ringSize = 1;
    while (ringSize < yin_.getFrameLength() + analysisHop)
        ringSize <<= 1;

    ring_.assign (static_cast<size_t> (ringSize), 0.0f);
    frame_.assign (static_cast<size_t> (ringSize), 0.0f);
    ringMask_ = ringSize - 1;

    setInputType (inputType_);
    setTracking (looseness_);
    reset();
}

void PitchAnalyzer::reset() noexcept
{
    decimator_.reset();
    voicing_.reset();
    std::fill (ring_.begin(), ring_.end(), 0.0f);
    ringWrite_ = hostCounter_ = crossings_ = octaveCounter_ = unvoicedHops_ = 0;
    sumSquares_ = 0.0;
    lastSample_ = trackedHz_ = lastPeriod_ = 0.0f;
    samplesSeen_ = 0;
    estimate_ = {};
}

void PitchAnalyzer::setInputType (InputType type) noexcept
{
    inputType_ = type;
    const auto range = pitchRangeFor (type);
    yin_.configure (range.minHz, range.maxHz);
    trackedHz_ = 0.0f;
    octaveCounter_ = 0;
}

void PitchAnalyzer::setTracking (float looseness01) noexcept
{
    looseness_ = std::clamp (looseness01, 0.0f, 1.0f);
    const float tight = 1.0f - looseness_;
    threshold_ = 0.10f + 0.25f * looseness_;     // YIN absolute threshold
    octaveMargin_ = 0.03f + 0.25f * tight;       // how strongly we prefer the previous octave
    octaveConfirmHops_ = 1 + static_cast<int> (std::lround (4.0f * tight));
    voicing_.setGateDb (-50.0f - 10.0f * looseness_);
    voicing_.setZcrHeadroomHz (2000.0f * looseness_);
}

int PitchAnalyzer::getAnalysisDelaySamples() const noexcept
{
    // YIN integrates over the newest W + period samples, so the estimate describes the
    // centre of that span. Before the first estimate assume the longest period.
    const float period = lastPeriod_ > 0.0f ? lastPeriod_ : static_cast<float> (yin_.getTauMax());
    const int factor = decimator_.getFactor();
    return static_cast<int> ((static_cast<float> (yin_.getWindowLength()) + period) * 0.5f * static_cast<float> (factor))
         + factor; // + decimator group delay (~1 analysis sample)
}

bool PitchAnalyzer::push (float x) noexcept
{
    ++samplesSeen_;
    sumSquares_ += double (x) * x;
    if ((x >= 0.0f) != (lastSample_ >= 0.0f))
        ++crossings_;
    lastSample_ = x;

    float decimated;
    if (decimator_.process (x, decimated))
    {
        ring_[static_cast<size_t> (ringWrite_)] = decimated;
        ringWrite_ = (ringWrite_ + 1) & ringMask_;
    }

    if (++hostCounter_ < getHopSizeHostSamples())
        return false;

    analyseHop();
    hostCounter_ = crossings_ = 0;
    sumSquares_ = 0.0;
    return true;
}

void PitchAnalyzer::analyseHop() noexcept
{
    const int hopHost = getHopSizeHostSamples();
    const int n = yin_.getFrameLength();

    // Unwrap the newest n analysis samples, newest first (see YinPitchDetector).
    int read = (ringWrite_ - 1) & ringMask_;
    for (int i = 0; i < n; ++i, read = (read - 1) & ringMask_)
        frame_[static_cast<size_t> (i)] = ring_[static_cast<size_t> (read)];

    auto result = yin_.analyse (frame_.data(), threshold_);

    // Octave-error rejection: if the new estimate is ~an octave from what we were
    // tracking and the old period still has a competitive dip, hold the old octave
    // for a few hops. Tighter tracking => larger margin and longer confirmation.
    if (result.belowThreshold && trackedHz_ > 0.0f)
    {
        const float octaves = std::abs (std::log2 (result.f0Hz / trackedHz_));
        const bool octaveJump = std::abs (octaves - 1.0f) < 0.1f || std::abs (octaves - 2.0f) < 0.1f;

        if (octaveJump)
        {
            const auto prevPeriod = static_cast<float> (yin_.getAnalysisRate() / trackedHz_);
            const auto alt = yin_.bestDipNear (prevPeriod, 0.06f);

            if (alt.periodSamples > 0.0f && alt.aperiodicity < result.aperiodicity + octaveMargin_
                && ++octaveCounter_ < octaveConfirmHops_)
            {
                result = alt;
                result.belowThreshold = alt.aperiodicity < threshold_ + octaveMargin_;
            }
            else
            {
                octaveCounter_ = 0;
            }
        }
        else
        {
            octaveCounter_ = 0;
        }
    }

    VoicingDetector::Features features;
    features.rmsDb = static_cast<float> (10.0 * std::log10 (sumSquares_ / hopHost + 1.0e-12));
    features.zcrHz = static_cast<float> (0.5 * crossings_ * sampleRate_ / hopHost);
    features.f0Hz = result.f0Hz;
    features.periodic = result.belowThreshold;
    features.confidence = 1.0f - result.aperiodicity;

    const bool voiced = voicing_.update (features);

    estimate_.confidence = std::clamp (1.0f - result.aperiodicity, 0.0f, 1.0f);
    estimate_.rmsDb = features.rmsDb;
    estimate_.zcrHz = features.zcrHz;
    estimate_.periodic = result.belowThreshold;
    estimate_.voiced = voiced;
    lastPeriod_ = result.periodSamples > 0.0f ? result.periodSamples : 0.0f;
    estimate_.centreSample = samplesSeen_ - getAnalysisDelaySamples();

    if (result.belowThreshold && voicing_.isCandidate (features))
    {
        trackedHz_ = result.f0Hz;
        estimate_.f0Hz = result.f0Hz;
        unvoicedHops_ = 0;
    }
    else if (++unvoicedHops_ * hopHost > static_cast<int> (0.1 * sampleRate_))
    {
        trackedHz_ = 0.0f; // forget octave context after ~100 ms of non-voice
    }
}

} // namespace evox::dsp
