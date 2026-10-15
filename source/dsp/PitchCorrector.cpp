#include "PitchCorrector.h"
#include "InputType.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evox::dsp
{

void PitchCorrector::setSettings (const CorrectorSettings& s) noexcept
{
    settings_ = s;
    quantizer_.configure (s.key, s.scale, s.noteModes);
}

void PitchCorrector::reset() noexcept
{
    note_ = ScaleQuantizer::noTarget;
    wasVoiced_ = false;
    correction_ = heldSeconds_ = vibratoFade_ = 0.0f;
    lfoPhase_ = 0.0;
}

float PitchCorrector::vibratoLfo() const noexcept
{
    const auto phase = static_cast<float> (lfoPhase_); // 0..1
    switch (settings_.vibratoShape)
    {
        case VibratoShape::square: return phase < 0.5f ? 1.0f : -1.0f;
        case VibratoShape::saw:    return 2.0f * phase - 1.0f;
        case VibratoShape::sine:   break;
    }
    return std::sin (2.0f * std::numbers::pi_v<float> * phase);
}

Correction PitchCorrector::update (float inputHz, bool voiced, float dt) noexcept
{
    Correction out;
    out.voiced = voiced && inputHz > 0.0f;

    if (! out.voiced)
    {
        wasVoiced_ = false;
        note_ = ScaleQuantizer::noTarget;
        return out;
    }

    const float in = hzToMidi (inputHz, settings_.referenceHz);
    out.inputMidi = in;

    // --- note-centre tracker (for Natural Vibrato) --------------------------------
    const bool onset = ! wasVoiced_;
    if (onset)
    {
        correction_ = 0.0f;
        centre1_ = centre2_ = in;
    }
    wasVoiced_ = true;

    // --- target selection on the detuned grid ------------------------------------
    const float detune = settings_.detuneCents * 0.01f;
    const int previousNote = note_;
    note_ = quantizer_.quantize (in - detune, note_);

    if (onset || note_ != previousNote)
    {
        heldSeconds_ = 0.0f;
        vibratoFade_ = 0.0f;
        centre1_ = centre2_ = in;   // don't let the note step leak into the vibrato estimate
        lfoPhase_ = 0.0;
    }
    else
    {
        heldSeconds_ += dt;
    }

    const float centreCoeff = 1.0f - std::exp (-dt * 2.0f * std::numbers::pi_v<float> * 1.5f);
    centre1_ += (in - centre1_) * centreCoeff;
    centre2_ += (centre1_ - centre2_) * centreCoeff;
    vibratoFade_ = std::min (1.0f, vibratoFade_ + dt / 0.15f);
    const float naturalVibrato = (in - centre2_) * vibratoFade_;

    // --- retune ------------------------------------------------------------------
    float corrected = in;
    if (note_ > ScaleQuantizer::bypassTarget)
    {
        const float target = static_cast<float> (note_) + detune;
        out.targetMidi = target;

        // Humanize: after ~120 ms on one note, ease the time constant up by as much as
        // 400 ms over the next 500 ms. Short notes keep the raw retune speed.
        const float held = std::clamp ((heldSeconds_ - 0.12f) / 0.5f, 0.0f, 1.0f);
        const float humanizeMs = settings_.humanize * 400.0f * held * held * (3.0f - 2.0f * held);
        const float tauSeconds = (settings_.retuneMs + humanizeMs) * 0.001f;

        const float desired = target - in;
        if (tauSeconds <= 0.0f)
            correction_ = desired; // instant: no smoothing whatsoever
        else
            correction_ += (desired - correction_) * (1.0f - std::exp (-dt / tauSeconds));

        corrected = in + correction_;
    }
    else
    {
        correction_ = 0.0f; // bypassed note / nothing allowed: leave the voice alone
    }

    // Natural Vibrato: 0 dB leaves the correction's result untouched. Other settings
    // add (g - 1) x the detected vibrato, so at slow retune it scales the singer's
    // vibrato by g and at fast retune it re-injects it on top of the flat note.
    const float vibratoGain = std::pow (10.0f, settings_.naturalVibratoDb / 20.0f);
    corrected += (vibratoGain - 1.0f) * naturalVibrato;

    // Create Vibrato: free-running from note start after the onset delay, 100 ms fade-in.
    if (settings_.vibratoOn)
    {
        const float sinceDelay = heldSeconds_ - settings_.vibratoDelayMs * 0.001f;
        if (sinceDelay > 0.0f)
        {
            lfoPhase_ = std::fmod (lfoPhase_ + double (settings_.vibratoRateHz) * dt, 1.0);
            const float fade = std::min (1.0f, sinceDelay / 0.1f);
            corrected += fade * settings_.vibratoDepthCents * 0.01f * vibratoLfo();
        }
    }

    out.outputMidi = corrected + settings_.transpose;
    return out;
}

} // namespace evox::dsp
