#include "EvoxEngine.h"

#include <algorithm>
#include <cmath>

namespace evox::dsp
{

void EvoxEngine::prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/)
{
    sampleRate_ = sampleRate;
    coreLatency_ = coreLatencyFor (sampleRate);
    latency_ = latencyFor (sampleRate);

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        conditioners_[static_cast<size_t> (ch)].prepare (sampleRate);
        dryDelay_[static_cast<size_t> (ch)].prepare (latency_);
        dryDelay_[static_cast<size_t> (ch)].setDelay (latency_);
        wetPad_[static_cast<size_t> (ch)].prepare (oversamplingDelay);
        wetPad_[static_cast<size_t> (ch)].setDelay (oversamplingDelay);
    }

    analyzer_.prepare (sampleRate);
    const float lowest = pitchRangeFor (InputType::instrument).minHz;
    shifter_.prepare (sampleRate, coreLatency_, lowest);
    // 2x stage: its index n holds base-rate audio from (n - upsampler delay) / 2.
    shifter2x_.prepare (2.0 * sampleRate, 2 * coreLatency_, lowest);
    shifter2x_.setTrackMapping (2.0, double (HalfbandKernel::centre));
    oversamplingFade_.prepare (sampleRate, 0.005);
    warmupLength_ = static_cast<int> (0.06 * sampleRate); // > 3 periods of the lowest note

    mix_.prepare (sampleRate, 0.02);
    gain_.prepare (sampleRate, 0.02);
    bypassFade_.prepare (sampleRate, 0.01);
    voicedFade_.prepare (sampleRate, 0.004); // short crossfade between shifted and dry consonants

    // Level match: 30 ms energy followers, 80 ms gain smoothing.
    energyCoeff_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.030 * sampleRate)));
    levelCoeff_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.080 * sampleRate)));

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
        wetPad_[static_cast<size_t> (ch)].reset();
        upsamplers_[static_cast<size_t> (ch)].reset();
        downsamplers_[static_cast<size_t> (ch)].reset();
    }
    analyzer_.reset();
    corrector_.reset();
    track_.reset();
    voicingCursor_ = {};
    shifter_.reset();
    shifter2x_.reset();
    oversamplingActive_ = params_.oversample2x;
    oversamplingFade_.setImmediate (1.0f);
    warmupRemaining_ = 0;
    now_ = 0;
    detectedPeriod_ = 0.0f;
    detectedVoiced_ = false;
    energyIn_ = energyWet_ = 0.0f;
    levelGain_ = 1.0f;
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

    corrector_.setSettings (p.correction);
    shifter_.setSettings (p.shifter);
    shifter2x_.setSettings (p.shifter);

    // Oversampling switch: warm the incoming stage for 60 ms, fade out, swap, fade in
    // (state machine in process()).
    if (! paramsInitialised_)
        oversamplingActive_ = p.oversample2x;
    else if (p.oversample2x != oversamplingActive_ && warmupRemaining_ == 0 && oversamplingFade_.getTarget() > 0.0f)
        warmupRemaining_ = warmupLength_;

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
    const float hopSeconds = static_cast<float> (analyzer_.getHopSizeHostSamples() / sampleRate_);
    const auto c = corrector_.update (e.f0Hz, e.voiced, hopSeconds);

    detectedVoiced_ = c.voiced;
    detectedPeriod_ = c.voiced ? static_cast<float> (sampleRate_ / e.f0Hz) : 0.0f;

    CorrectionTrack::Entry entry;
    entry.sample = e.centreSample;
    entry.voiced = c.voiced;
    entry.periodSamples = detectedPeriod_;
    entry.ratio = std::exp2 (c.shiftSemitones() / 12.0f);
    track_.push (entry);

    const float ref = params_.correction.referenceHz;
    AnalysisFrame frame;
    frame.sample = e.centreSample;
    frame.inputHz = c.voiced ? e.f0Hz : 0.0f;
    frame.outputHz = c.voiced ? midiToHz (c.outputMidi, ref) : 0.0f;
    frame.targetMidi = c.targetMidi;
    frame.correctionCents = 100.0f * c.shiftSemitones();
    frame.confidence = e.confidence;
    frame.inputDb = e.rmsDb;
    frame.voiced = e.voiced;
    analysisFifo_.push (frame); // dropping a frame when the UI is behind is harmless
}

void EvoxEngine::measure (float* const* channels, int numChannels, int numSamples, bool input) noexcept
{
    if (numSamples <= 0)
        return;
    float peak = 0.0f;
    double sum = 0.0;
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = channels[ch][i];
            peak = std::max (peak, std::abs (v));
            sum += double (v) * v;
        }
    const auto rms = static_cast<float> (std::sqrt (sum / (numSamples * numChannels)));
    LevelMeters::raise (input ? meters_.inPeak : meters_.outPeak, peak);
    (input ? meters_.inRms : meters_.outRms).store (rms, std::memory_order_relaxed);
}

void EvoxEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, maxChannels);
    if (numChannels <= 0)
        return;

    measure (channels, numChannels, numSamples, true);

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

        mono *= monoScale;
        if (analyzer_.push (mono))
            onNewEstimate();

        // The 1x stage is always fed (it also serves the unshifted/unvoiced path). The 2x
        // stage is fed only while active or warming up for a switch.
        shifter_.pushInput (conditioned, numChannels, mono, now_, detectedPeriod_, detectedVoiced_);
        if (oversamplingActive_ || warmupRemaining_ > 0 || params_.oversample2x != oversamplingActive_)
        {
            float up0[maxChannels] {}, up1[maxChannels] {};
            float mono0 = 0.0f, mono1 = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                upsamplers_[static_cast<size_t> (ch)].process (conditioned[ch], up0[ch], up1[ch]);
                mono0 += up0[ch];
                mono1 += up1[ch];
            }
            shifter2x_.pushInput (up0, numChannels, mono0 * monoScale, 2 * now_, 2.0f * detectedPeriod_, detectedVoiced_);
            shifter2x_.pushInput (up1, numChannels, mono1 * monoScale, 2 * now_ + 1, 2.0f * detectedPeriod_, detectedVoiced_);
        }

        // Oversampling switch: warm-up done -> fade out; faded out -> swap, restart, fade in.
        if (warmupRemaining_ > 0 && --warmupRemaining_ == 0)
            oversamplingFade_.setTarget (0.0f);

        if (oversamplingActive_ != params_.oversample2x && warmupRemaining_ == 0 && ! oversamplingFade_.isSmoothing()
            && oversamplingFade_.getTarget() <= 0.0f)
        {
            oversamplingActive_ = params_.oversample2x;
            (oversamplingActive_ ? shifter2x_ : shifter_).restartSynthesis();
            for (int ch = 0; ch < maxChannels; ++ch)
            {
                wetPad_[static_cast<size_t> (ch)].reset();
                downsamplers_[static_cast<size_t> (ch)].reset();
            }
            oversamplingFade_.setTarget (1.0f);
        }

        // Output point: the input sample `latency` samples ago. The PSOLA stage renders
        // `coreLatency` behind the input; the 31-sample remainder is the half-band filters
        // at 2x, or an equal fixed pad at 1x.
        const std::int64_t p = now_ - latency_;
        const std::int64_t renderPoint = now_ - coreLatency_;

        float shifted[maxChannels] {};
        if (renderPoint >= 0)
        {
            if (oversamplingActive_)
            {
                float a[maxChannels] {}, b[maxChannels] {};
                shifter2x_.render (2 * renderPoint, track_, a, numChannels);
                shifter2x_.render (2 * renderPoint + 1, track_, b, numChannels);
                for (int ch = 0; ch < numChannels; ++ch)
                    shifted[ch] = downsamplers_[static_cast<size_t> (ch)].process (a[ch], b[ch]);
            }
            else
            {
                shifter_.render (renderPoint, track_, shifted, numChannels);
                for (int ch = 0; ch < numChannels; ++ch)
                    shifted[ch] = wetPad_[static_cast<size_t> (ch)].process (shifted[ch]);
            }
        }
        if (p >= 0)
            voicedFade_.setTarget (track_.at (voicingCursor_, p).voiced ? 1.0f : 0.0f);
        ++now_;

        // Unvoiced audio (s, t, breaths) skips the shifter: crossfade to the unshifted input.
        const float voiced = voicedFade_.next();

        // Level match. Pitch-synchronous grains centred on glottal peaks (more so with hard
        // Character, or when lowering pitch) carry more energy than the source; follow the
        // ratio slowly (80 ms) on voiced audio only, within +/-12 dB, so it can't pump.
        if (p >= 0 && voiced > 0.5f)
        {
            float inSq = 0.0f, wetSq = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float u = shifter_.readInput (ch, p);
                inSq += u * u;
                wetSq += shifted[ch] * shifted[ch];
            }
            energyIn_ += (inSq - energyIn_) * energyCoeff_;
            energyWet_ += (wetSq - energyWet_) * energyCoeff_;
            const float target = energyWet_ > 1.0e-9f ? std::clamp (std::sqrt (energyIn_ / energyWet_), 0.25f, 4.0f) : 1.0f;
            levelGain_ += (target - levelGain_) * levelCoeff_;
        }
        // During an oversampling switch the shifted path dips towards the unshifted input
        // (never to silence) for 5 ms each way.
        const float osFade = oversamplingFade_.next();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float u = p >= 0 ? shifter_.readInput (ch, p) : 0.0f;
            shifted[ch] = osFade * shifted[ch] * levelGain_ + (1.0f - osFade) * u;
        }
        const float mix = mix_.next();
        const float gain = gain_.next();
        const float bypass = bypassFade_.next();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<size_t> (ch);
            const float dry = dryDelay_[c].process (channels[ch][i]);
            const float unshifted = p >= 0 ? shifter_.readInput (ch, p) : 0.0f;
            const float wet = voiced * shifted[ch] + (1.0f - voiced) * unshifted;
            const float processed = (mix * wet + (1.0f - mix) * dry) * gain;
            channels[ch][i] = bypass * dry + (1.0f - bypass) * processed;
        }
    }

    measure (channels, numChannels, numSamples, false);
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
