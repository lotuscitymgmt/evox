#pragma once

#include "Decimator.h"
#include "InputType.h"
#include "VoicingDetector.h"
#include "YinPitchDetector.h"

#include <cstdint>
#include <vector>

namespace evox::dsp
{

/** One pitch/voicing decision, produced once per analysis hop. */
struct PitchEstimate
{
    float f0Hz = 0.0f;          // last reliable f0 (held through unvoiced gaps)
    float confidence = 0.0f;    // 1 - aperiodicity, 0..1
    float rmsDb = -120.0f;
    float zcrHz = 0.0f;
    bool periodic = false;      // YIN dip crossed the tracking threshold
    bool voiced = false;        // final decision after voicing detector + hysteresis
    std::int64_t centreSample = 0; // host-rate input sample index the estimate describes
};

/** Stage 2: streaming pitch tracker.
    Decimates the conditioned mono signal, runs YIN every hop on the most recent
    window, applies octave-error rejection scaled by "Tracking", and runs the
    voiced/unvoiced detector. Real-time safe after prepare(). */
class PitchAnalyzer
{
public:
    static constexpr int analysisHop = 64; // samples at the analysis rate (~2.7 ms)

    void prepare (double sampleRate);
    void reset() noexcept;

    void setInputType (InputType type) noexcept;
    /** 0 = tight (rejects octave errors, needs clean voice), 1 = loose (follows rough/breathy voices). */
    void setTracking (float looseness01) noexcept;

    /** Feed one host-rate sample. Returns true when a new estimate is ready. */
    bool push (float x) noexcept;

    const PitchEstimate& getEstimate() const noexcept { return estimate_; }
    const YinPitchDetector& getDetector() const noexcept { return yin_; }

    int getHopSizeHostSamples() const noexcept { return analysisHop * decimator_.getFactor(); }
    /** Host-rate samples between the newest input sample and the centre of the analysis window. */
    int getAnalysisDelaySamples() const noexcept;
    float getThreshold() const noexcept { return threshold_; }

private:
    void analyseHop() noexcept;

    Decimator decimator_;
    YinPitchDetector yin_;
    VoicingDetector voicing_;
    InputType inputType_ = InputType::altoTenor;

    std::vector<float> ring_, frame_;
    int ringMask_ = 0, ringWrite_ = 0;

    double sampleRate_ = 48000.0;
    float looseness_ = 0.5f, threshold_ = 0.2f, octaveMargin_ = 0.15f;
    int octaveConfirmHops_ = 3, octaveCounter_ = 0;
    int hostCounter_ = 0, crossings_ = 0, unvoicedHops_ = 0;
    double sumSquares_ = 0.0;
    float lastSample_ = 0.0f, trackedHz_ = 0.0f, lastPeriod_ = 0.0f;
    std::int64_t samplesSeen_ = 0;
    PitchEstimate estimate_;
};

} // namespace evox::dsp
