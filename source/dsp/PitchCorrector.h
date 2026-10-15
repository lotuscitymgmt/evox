#pragma once

#include "ScaleQuantizer.h"

namespace evox::dsp
{

enum class VibratoShape
{
    sine = 0,
    square,
    saw
};

struct CorrectorSettings
{
    int key = 0;
    ScaleType scale = ScaleType::chromatic;
    std::array<NoteMode, 12> noteModes {};

    float retuneMs = 0.0f;          // 0 = instant, stepped
    float humanize = 0.0f;          // 0..1
    float naturalVibratoDb = 0.0f;  // -12..+12
    float transpose = 0.0f;         // semitones
    float detuneCents = 0.0f;
    float referenceHz = 440.0f;

    bool vibratoOn = false;
    VibratoShape vibratoShape = VibratoShape::sine;
    float vibratoRateHz = 5.5f;
    float vibratoDepthCents = 30.0f;
    float vibratoDelayMs = 300.0f;
};

/** Result of one correction step. Pitches are MIDI note numbers (fractional). */
struct Correction
{
    float inputMidi = 0.0f;
    float outputMidi = 0.0f;
    float targetMidi = -1.0f; // -1 = no target (bypassed note / nothing allowed / unvoiced)
    bool voiced = false;

    float shiftSemitones() const noexcept { return voiced ? outputMidi - inputMidi : 0.0f; }
};

/** Stage 3b: turns a detected pitch into the desired output pitch.

    Model: output = input + c, where the correction c chases (target - input) with the
    Retune Speed time constant. Correcting the *offset* rather than the output means:
      - at 0 ms, c is set exactly every step, so the output is the target - flat, and it
        jumps the instant the target note changes (the stepped hard-tune signature);
      - at slower speeds the singer's own pitch movement passes through and is only
        gradually pulled in.
    Humanize lengthens the time constant as a note is held. Natural Vibrato separates the
    input's vibrato (deviation from a slow note-centre tracker) and re-injects it scaled,
    independent of the retune speed. Create Vibrato, Transpose and Detune are applied last.
    Runs once per analysis hop; real-time safe. */
class PitchCorrector
{
public:
    void setSettings (const CorrectorSettings& s) noexcept;
    void reset() noexcept;

    Correction update (float inputHz, bool voiced, float dtSeconds) noexcept;

    int getCurrentNote() const noexcept { return note_; }

private:
    float vibratoLfo() const noexcept;

    CorrectorSettings settings_;
    ScaleQuantizer quantizer_;

    int note_ = ScaleQuantizer::noTarget;
    bool wasVoiced_ = false;
    float correction_ = 0.0f;   // c, semitones
    float heldSeconds_ = 0.0f;  // time on the current note
    float centre1_ = 0.0f, centre2_ = 0.0f; // 2-pole note-centre tracker
    float vibratoFade_ = 0.0f;
    double lfoPhase_ = 0.0;
};

} // namespace evox::dsp
