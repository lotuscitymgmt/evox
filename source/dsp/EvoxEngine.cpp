#include "EvoxEngine.h"

#include <algorithm>
#include <cmath>

namespace evox::dsp
{

void EvoxEngine::prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/)
{
    sampleRate_ = sampleRate;
    latency_ = latencyFor (sampleRate);

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        conditioners_[static_cast<size_t> (ch)].prepare (sampleRate);
        dryDelay_[static_cast<size_t> (ch)].prepare (latency_);
        dryDelay_[static_cast<size_t> (ch)].setDelay (latency_);
        wetDelay_[static_cast<size_t> (ch)].prepare (latency_);
        wetDelay_[static_cast<size_t> (ch)].setDelay (latency_);
    }

    analyzer_.prepare (sampleRate);

    mix_.prepare (sampleRate, 0.02);
    gain_.prepare (sampleRate, 0.02);
    bypassFade_.prepare (sampleRate, 0.01);
    voicedFade_.prepare (sampleRate, 0.004); // short crossfade between shifted and dry consonants

    paramsInitialised_ = false;
    setParams (params_);
    reset();
}

void EvoxEngine::reset() noexcept
{
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        conditioners_[static_cast<size_t> (ch)].reset();
        dryDelay_[static_cast<size_t> (ch)].reset();
        wetDelay_[static_cast<size_t> (ch)].reset();
    }
    analyzer_.reset();
    voicedFade_.setImmediate (0.0f);
}

void EvoxEngine::setParams (const EngineParams& p) noexcept
{
    if (! paramsInitialised_ || p.inputType != params_.inputType)
        analyzer_.setInputType (p.inputType);
    if (! paramsInitialised_ || p.tracking != params_.tracking)
        analyzer_.setTracking (p.tracking);

    for (auto& c : conditioners_)
        c.setLowCutEnabled (p.lowCut);

    const float gainLin = std::pow (10.0f, p.outputGainDb / 20.0f);
    const float bypassTarget = p.bypass ? 1.0f : 0.0f;

    if (! paramsInitialised_)
    {
        mix_.setImmediate (p.mix);
        gain_.setImmediate (gainLin);
        bypassFade_.setImmediate (bypassTarget);
    }
    else
    {
        mix_.setTarget (p.mix);
        gain_.setTarget (gainLin);
        bypassFade_.setTarget (bypassTarget);
    }

    params_ = p;
    paramsInitialised_ = true;
}

void EvoxEngine::onNewEstimate() noexcept
{
    const auto& e = analyzer_.getEstimate();
    voicedFade_.setTarget (e.voiced ? 1.0f : 0.0f);

    AnalysisFrame frame;
    frame.sample = e.centreSample;
    frame.inputHz = e.voiced ? e.f0Hz : 0.0f;
    frame.outputHz = frame.inputHz;
    frame.confidence = e.confidence;
    frame.inputDb = e.rmsDb;
    frame.voiced = e.voiced;
    analysisFifo_.push (frame); // dropping a frame when the UI is behind is harmless
}

void EvoxEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, maxChannels);
    if (numChannels <= 0)
        return;

    const float monoScale = 1.0f / static_cast<float> (numChannels);

    for (int i = 0; i < numSamples; ++i)
    {
        float conditioned[maxChannels] {};
        float mono = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            conditioned[ch] = conditioners_[static_cast<size_t> (ch)].process (channels[ch][i]);
            mono += conditioned[ch];
        }

        if (analyzer_.push (mono * monoScale))
            onNewEstimate();

        // Voiced/unvoiced weight. Until the shifter lands (Phase 3) both paths carry the
        // same conditioned signal, so this is sonically neutral but already exercised.
        const float voiced = voicedFade_.next();
        const float mix = mix_.next();
        const float gain = gain_.next();
        const float bypass = bypassFade_.next();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<size_t> (ch);
            const float dry = dryDelay_[c].process (channels[ch][i]);
            const float unshifted = wetDelay_[c].process (conditioned[ch]);
            const float shifted = unshifted;
            const float wet = voiced * shifted + (1.0f - voiced) * unshifted;
            const float processed = (mix * wet + (1.0f - mix) * dry) * gain;
            channels[ch][i] = bypass * dry + (1.0f - bypass) * processed;
        }
    }
}

void EvoxEngine::processBypassed (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, maxChannels);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& delay = dryDelay_[static_cast<size_t> (ch)];
        for (int i = 0; i < numSamples; ++i)
            channels[ch][i] = delay.process (channels[ch][i]);
    }
}

} // namespace evox::dsp
