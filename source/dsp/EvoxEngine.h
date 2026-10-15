#pragma once

#include "DelayLine.h"
#include "InputConditioner.h"
#include "PitchAnalyzer.h"
#include "Smoother.h"
#include "SpscFifo.h"

#include <array>
#include <cstdint>

namespace evox::dsp
{

/** Snapshot of the engine's parameters, written once per block by the host wrapper.
    Plain data so it can be filled from JUCE, AAX, or a test harness alike. */
struct EngineParams
{
    InputType inputType = InputType::altoTenor;
    float tracking = 0.5f;      // 0 tight .. 1 loose
    bool lowCut = true;
    float mix = 1.0f;           // 0 dry .. 1 wet
    float outputGainDb = 0.0f;
    bool bypass = false;
};

/** Data the UI needs, published once per analysis hop through a lock-free FIFO. */
struct AnalysisFrame
{
    std::int64_t sample = 0;    // host-rate input sample index of the window centre
    float inputHz = 0.0f;       // detected pitch (0 = none)
    float outputHz = 0.0f;      // corrected pitch (== input until Phase 3)
    float targetMidi = -1.0f;   // target note, -1 = none
    float correctionCents = 0.0f;
    float confidence = 0.0f;
    float inputDb = -120.0f;
    bool voiced = false;
};

/** Top-level real-time engine. Owns every DSP stage and the latency budget.
    prepare() allocates; process() never allocates, locks or does I/O. */
class EvoxEngine
{
public:
    static constexpr int maxChannels = 2;
    /** Lookahead reserved for the TD-PSOLA synthesis stage (see README "Latency"). */
    static constexpr double lookaheadMs = 9.0;

    static int latencyFor (double sampleRate) noexcept
    {
        return static_cast<int> (lookaheadMs * 0.001 * sampleRate + 0.5);
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setParams (const EngineParams& p) noexcept;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Host-bypass path: only the latency-compensating delay, so toggling bypass never shifts timing. */
    void processBypassed (float* const* channels, int numChannels, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return latency_; }
    double getSampleRate() const noexcept { return sampleRate_; }

    SpscFifo<AnalysisFrame, 1024>& getAnalysisFifo() noexcept { return analysisFifo_; }
    const PitchAnalyzer& getAnalyzer() const noexcept { return analyzer_; }

private:
    void onNewEstimate() noexcept;

    double sampleRate_ = 48000.0;
    int latency_ = 0;
    EngineParams params_;
    bool paramsInitialised_ = false;

    std::array<InputConditioner, maxChannels> conditioners_;
    std::array<DelayLine, maxChannels> dryDelay_, wetDelay_;
    PitchAnalyzer analyzer_;

    LinearSmoother mix_, gain_, bypassFade_, voicedFade_;
    SpscFifo<AnalysisFrame, 1024> analysisFifo_;
};

} // namespace evox::dsp
