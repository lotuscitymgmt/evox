#include "TestFramework.h"
#include "ShifterTestUtils.h"

#include "dsp/PitchCorrector.h"
#include "dsp/ScaleQuantizer.h"

#include <string>

using namespace evox;
using dsp::NoteMode;

namespace
{
std::array<NoteMode, 12> allModes (NoteMode m)
{
    std::array<NoteMode, 12> a;
    a.fill (m);
    return a;
}

constexpr float hop = 128.0f / 48000.0f;

dsp::Correction runConstant (dsp::PitchCorrector& c, float hz, float seconds)
{
    dsp::Correction r;
    for (float t = 0.0f; t < seconds; t += hop)
        r = c.update (hz, true, hop);
    return r;
}
} // namespace

TEST_CASE ("quantizer: scales, removed and bypassed notes")
{
    dsp::ScaleQuantizer q;
    q.setHysteresis (0.0f);
    q.configure (0, dsp::ScaleType::major, allModes (NoteMode::scale));

    CHECK (q.quantize (61.4f, q.noTarget) == 62);   // C#+40 -> D in C major
    CHECK (q.quantize (60.6f, q.noTarget) == 60);   // C+60  -> C (C# not in scale)
    CHECK (q.quantize (64.6f, q.noTarget) == 65);   // E-F semitone step
    CHECK (q.quantize (70.4f, q.noTarget) == 71);   // A#+40 -> B

    q.configure (9, dsp::ScaleType::minor, allModes (NoteMode::scale)); // A minor == C major
    CHECK (q.quantize (61.4f, q.noTarget) == 62);
    q.configure (2, dsp::ScaleType::major, allModes (NoteMode::scale)); // D major has F#, C#
    CHECK (q.quantize (66.2f, q.noTarget) == 66);
    CHECK (q.quantize (61.2f, q.noTarget) == 61);

    auto modes = allModes (NoteMode::scale);
    modes[2] = NoteMode::removed;  // remove D
    modes[4] = NoteMode::bypassed; // bypass E
    modes[1] = NoteMode::enabled;  // force C#
    q.configure (0, dsp::ScaleType::major, modes);
    CHECK (q.quantize (62.1f, q.noTarget) == 61);   // D removed: nearest allowed C#
    CHECK (q.quantize (63.9f, q.noTarget) == dsp::ScaleQuantizer::bypassTarget);
    CHECK (q.quantize (61.1f, q.noTarget) == 61);

    q.configure (0, dsp::ScaleType::chromatic, allModes (NoteMode::removed));
    CHECK (q.quantize (60.0f, q.noTarget) == dsp::ScaleQuantizer::noTarget);
}

TEST_CASE ("quantizer: hysteresis prevents chattering at a boundary")
{
    dsp::ScaleQuantizer q;
    q.configure (0, dsp::ScaleType::chromatic, allModes (NoteMode::scale));
    q.setHysteresis (0.15f);
    int note = q.quantize (60.0f, q.noTarget);
    CHECK (note == 60);
    for (float m : { 60.45f, 60.55f, 60.57f, 60.5f, 60.55f })
        note = q.quantize (m, note);
    CHECK (note == 60);                 // within +/-0.075 of the midpoint: hold
    note = q.quantize (60.6f, note);
    CHECK (note == 61);                 // clearly past it: switch
    note = q.quantize (60.45f, note);
    CHECK (note == 61);                 // and hold on the way back
}

TEST_CASE ("corrector: 0 ms retune is exact and instantaneous")
{
    dsp::PitchCorrector c;
    dsp::CorrectorSettings s;
    s.retuneMs = 0.0f;
    c.setSettings (s);

    // 30 ct sharp A4 -> exactly 69 on the very first voiced frame.
    auto r = c.update (440.0f * std::exp2 (0.3f / 12.0f), true, hop);
    CHECK_NEAR (r.outputMidi, 69.0f, 1.0e-4f);
    CHECK_NEAR (r.targetMidi, 69.0f, 1.0e-4f);

    // Crossing the boundary flips the output by a whole semitone in a single frame.
    r = c.update (440.0f * std::exp2 (0.62f / 12.0f), true, hop);
    CHECK_NEAR (r.outputMidi, 70.0f, 1.0e-4f);
}

TEST_CASE ("corrector: retune speed is a time constant")
{
    dsp::PitchCorrector c;
    dsp::CorrectorSettings s;
    s.retuneMs = 100.0f;
    c.setSettings (s);

    const float hz = 440.0f * std::exp2 (0.4f / 12.0f); // 40 ct sharp
    c.update (hz, true, hop);
    float t = hop;
    dsp::Correction r;
    while (t < 0.1f)
    {
        r = c.update (hz, true, hop);
        t += hop;
    }
    // After one time constant ~63% of the 0.40 st correction has been applied.
    const float applied = r.inputMidi - r.outputMidi;
    CHECK_NEAR (applied / 0.4f, 0.63f, 0.05f);
}

TEST_CASE ("corrector: humanize lets long notes drift, short notes still snap")
{
    dsp::CorrectorSettings s;
    s.retuneMs = 0.0f;
    s.humanize = 1.0f;
    dsp::PitchCorrector c;
    c.setSettings (s);

    // Short note: snaps.
    auto r = c.update (440.0f * std::exp2 (0.2f / 12.0f), true, hop);
    CHECK_NEAR (r.outputMidi, 69.0f, 1.0e-3f);

    // Hold for a second, then the singer drifts 20 ct: the drift now mostly survives.
    runConstant (c, 440.0f, 1.0f);
    r = c.update (440.0f * std::exp2 (0.2f / 12.0f), true, hop);
    CHECK_MSG (r.outputMidi - 69.0f > 0.18f, std::to_string (r.outputMidi));
}

TEST_CASE ("corrector: natural vibrato scaling")
{
    auto measureDepth = [] (float retuneMs, float vibratoDb) {
        dsp::CorrectorSettings s;
        s.retuneMs = retuneMs;
        s.naturalVibratoDb = vibratoDb;
        dsp::PitchCorrector c;
        c.setSettings (s);
        float lo = 1e9f, hi = -1e9f;
        for (float t = 0.0f; t < 2.0f; t += hop)
        {
            const float hz = 220.0f * std::exp2 (0.3f / 12.0f * std::sin (2.0f * 3.14159265f * 5.5f * t));
            const auto r = c.update (hz, true, hop);
            if (t > 1.0f)
            {
                lo = std::min (lo, r.outputMidi);
                hi = std::max (hi, r.outputMidi);
            }
        }
        return 0.5f * (hi - lo); // semitones, peak
    };

    // Slow retune: natural vibrato knob scales the singer's +/-30 ct vibrato.
    CHECK_NEAR (measureDepth (400.0f, 0.0f), 0.3f, 0.04f);
    CHECK_NEAR (measureDepth (400.0f, 6.0206f), 0.6f, 0.08f);
    CHECK_NEAR (measureDepth (400.0f, -12.0f), 0.075f, 0.04f);
    // Hard tune: flat by default, vibrato re-injected when the knob is raised.
    CHECK (measureDepth (0.0f, 0.0f) < 0.005f);
    CHECK_NEAR (measureDepth (0.0f, 6.0206f), 0.3f, 0.05f);
}

TEST_CASE ("corrector: create vibrato, transpose, detune, reference pitch")
{
    dsp::CorrectorSettings s;
    s.vibratoOn = true;
    s.vibratoShape = dsp::VibratoShape::square;
    s.vibratoDepthCents = 50.0f;
    s.vibratoRateHz = 4.0f;
    s.vibratoDelayMs = 200.0f;
    dsp::PitchCorrector c;
    c.setSettings (s);

    float before = 0.0f, lo = 1e9f, hi = -1e9f;
    for (float t = 0.0f; t < 1.5f; t += hop)
    {
        const auto r = c.update (440.0f, true, hop);
        if (t < 0.18f)
            before = std::max (before, std::abs (r.outputMidi - 69.0f));
        if (t > 0.5f)
        {
            lo = std::min (lo, r.outputMidi);
            hi = std::max (hi, r.outputMidi);
        }
    }
    CHECK (before < 1.0e-4f);           // onset delay respected
    CHECK_NEAR (hi, 69.5f, 1.0e-3f);   // square: exactly +/- depth
    CHECK_NEAR (lo, 68.5f, 1.0e-3f);

    dsp::CorrectorSettings t;
    t.transpose = 12.0f;
    t.detuneCents = 25.0f;
    c.setSettings (t);
    c.reset();
    auto r = c.update (440.0f, true, hop);
    CHECK_NEAR (r.outputMidi, 69.25f + 12.0f, 1.0e-3f); // grid shifted by +25 ct, then +12 st

    dsp::CorrectorSettings u;
    u.referenceHz = 415.0f; // baroque A
    c.setSettings (u);
    c.reset();
    r = c.update (415.0f, true, hop);
    CHECK_NEAR (r.outputMidi, 69.0f, 1.0e-3f);
    CHECK_NEAR (r.shiftSemitones(), 0.0f, 1.0e-3f);
}

// ----------------------------------------------------------------------------------
// End-to-end: engine output measured with the pitch tracker
// ----------------------------------------------------------------------------------

TEST_CASE ("shifter: corrects to the nearest note and transposes accurately")
{
    const double fs = 48000.0;
    struct Case { float inHz; float transpose; float expectHz; dsp::InputType type; };
    const Case cases[] = {
        { 210.0f, 0.0f, 207.652f, dsp::InputType::altoTenor },   // -> G#3
        { 210.0f, 12.0f, 415.305f, dsp::InputType::altoTenor },  // octave up
        { 210.0f, -12.0f, 103.826f, dsp::InputType::lowMale },   // octave down
        { 210.0f, 7.0f, 311.127f, dsp::InputType::altoTenor },   // fifth up
        { 450.0f, 0.0f, 440.0f, dsp::InputType::soprano },
        { 90.0f, 0.0f, 92.499f, dsp::InputType::lowMale },       // deep note (period > lookahead) -> F#2
        { 84.0f, 0.0f, 82.407f, dsp::InputType::lowMale },       // -> E2
    };

    for (const auto& c : cases)
    {
        for (int formant = 0; formant < 2; ++formant)
        {
            dsp::EngineParams p;
            p.inputType = c.inHz < 120.0f ? dsp::InputType::lowMale : dsp::InputType::altoTenor;
            p.correction.transpose = c.transpose;
            p.shifter.formantCorrection = formant == 1;
            const auto in = tools::synthVoice (fs, 1.5, tools::constantPitch (c.inHz));
            const auto out = test::renderEngine (in.samples, fs, p);
            const auto track = test::pitchTrack (out, fs, c.type, 0.2);
            const float got = test::medianHz (track);
            const std::string label = std::to_string (c.inHz) + "Hz tr " + std::to_string (c.transpose) + " formant "
                                    + std::to_string (formant) + ": got " + std::to_string (got) + " expect " + std::to_string (c.expectHz)
                                    + " voicedFrames " + std::to_string (track.size());
            CHECK_MSG (track.size() > 300, label);
            CHECK_MSG (std::abs (dsp::centsBetween (c.expectHz, got)) < 3.0f, label);

            // Level stays within ~2 dB of the input (unity-gain overlap-add).
            const double gainDb = 20.0 * std::log10 (test::rms (out, 24000, out.size()) / test::rms (in.samples, 24000, in.samples.size()));
            CHECK_MSG (std::abs (gainDb) < 2.0, label + " gain " + std::to_string (gainDb) + " dB");
        }
    }
}

TEST_CASE ("shifter: 0 ms retune produces stepped output; slow retune glides")
{
    const double fs = 48000.0;
    // Smooth glide C3 -> G3 over 1.5 s: input spends equal time at every cent.
    const auto in = tools::synthVoice (fs, 1.8, tools::logSweep (130.81, 196.0, 1.5));

    auto analyse = [&] (float retuneMs, int& onNote, int& between, int& total) {
        dsp::EngineParams p;
        p.correction.retuneMs = retuneMs;
        const auto out = test::renderEngine (in.samples, fs, p);
        onNote = between = total = 0;
        for (const auto& pt : test::pitchTrack (out, fs, dsp::InputType::altoTenor, 0.15))
        {
            if (pt.seconds > 1.45)
                break;
            const float midi = dsp::hzToMidi (pt.hz);
            const float off = std::abs (midi - std::round (midi));
            ++total;
            onNote += off < 0.08f ? 1 : 0;
            between += off > 0.2f ? 1 : 0;
        }
    };

    int on0, mid0, n0, onSlow, midSlow, nSlow;
    analyse (0.0f, on0, mid0, n0);
    analyse (250.0f, onSlow, midSlow, nSlow);

    const std::string msg = "0ms on " + std::to_string (on0) + " between " + std::to_string (mid0) + " / " + std::to_string (n0)
                          + " | 250ms on " + std::to_string (onSlow) + " between " + std::to_string (midSlow) + " / " + std::to_string (nSlow);
    CHECK_MSG (n0 > 300, msg);
    // Hard tune: nearly every frame sits on a semitone; only the step frames are in between.
    CHECK_MSG (on0 > n0 * 85 / 100, msg);
    CHECK_MSG (mid0 < n0 * 5 / 100, msg);
    // Slow retune: the glide survives, so lots of frames are between notes.
    CHECK_MSG (midSlow > nSlow * 30 / 100, msg);
}

TEST_CASE ("shifter: unity ratio reproduces the input level and pitch")
{
    const double fs = 44100.0;
    dsp::EngineParams p;
    p.correction.noteModes.fill (dsp::NoteMode::bypassed); // leave everything alone
    const auto in = tools::synthVoice (fs, 1.0, tools::withVibrato (tools::constantPitch (247.0), 5.5, 40.0));
    const auto out = test::renderEngine (in.samples, fs, p);

    const auto inTrack = test::pitchTrack (in.samples, fs, dsp::InputType::altoTenor);
    const auto outTrack = test::pitchTrack (out, fs, dsp::InputType::altoTenor);
    CHECK_NEAR (dsp::centsBetween (test::medianHz (inTrack), test::medianHz (outTrack)), 0.0f, 3.0f);
    const double gainDb = 20.0 * std::log10 (test::rms (out, 20000, out.size()) / test::rms (in.samples, 20000, in.samples.size()));
    CHECK_MSG (std::abs (gainDb) < 1.0, std::to_string (gainDb));
}

TEST_CASE ("shifter: unvoiced audio passes through unshifted")
{
    const double fs = 48000.0;
    dsp::EngineParams p;
    p.lowCut = false;
    p.correction.transpose = 12.0f;
    const auto noise = tools::sibilant (fs, 0.5, 0.2f);
    int latency = 0;
    const auto out = test::renderEngine (noise.samples, fs, p, &latency);

    double err = 0.0, ref = 0.0;
    for (size_t i = 4800; i < out.size(); ++i)
    {
        const double d = out[i] - noise.samples[i - static_cast<size_t> (latency)];
        err += d * d;
        ref += double (noise.samples[i]) * noise.samples[i];
    }
    CHECK_MSG (10.0 * std::log10 (err / ref) < -30.0, std::to_string (10.0 * std::log10 (err / ref)) + " dB");
}

TEST_CASE ("formant: correction keeps the envelope, throat moves it independently of pitch")
{
    const double fs = 48000.0;
    const auto in = tools::synthVoice (fs, 1.0, tools::constantPitch (150.0));
    const size_t from = 24000, n = 8192;
    const double d3 = 146.832; // 150 Hz snaps to D3 first
    const double inEnv = test::harmonicEnvelopeCentroid (in.samples, from, n, fs, 150.0);

    auto envelopeFor = [&] (bool formant, float throat, float transpose) {
        dsp::EngineParams p;
        p.correction.transpose = transpose;
        p.shifter.formantCorrection = formant;
        p.shifter.throat = throat;
        const auto out = test::renderEngine (in.samples, fs, p);
        return test::harmonicEnvelopeCentroid (out, from, n, fs, d3 * std::exp2 (transpose / 12.0));
    };

    const double upKept = envelopeFor (true, 1.0f, 12.0f);
    const double downKept = envelopeFor (true, 1.0f, -12.0f);
    const double upChipmunk = envelopeFor (false, 1.0f, 12.0f);
    const double downMonster = envelopeFor (false, 1.0f, -12.0f);
    const double throatNeutral = envelopeFor (true, 1.0f, 0.0f);
    const double throatLong = envelopeFor (true, 1.5f, 0.0f);
    const double throatShort = envelopeFor (true, 0.7f, 0.0f);

    const std::string msg = "in " + std::to_string (inEnv) + " | +12 kept " + std::to_string (upKept) + " -12 kept "
                          + std::to_string (downKept) + " | +12 off " + std::to_string (upChipmunk) + " -12 off "
                          + std::to_string (downMonster) + " | throat100 " + std::to_string (throatNeutral) + " throat150 " + std::to_string (throatLong) + " throat70 "
                          + std::to_string (throatShort);

    CHECK_MSG (std::abs (upKept / inEnv - 1.0) < 0.1, msg);   // formant correction: envelope stays put
    CHECK_MSG (std::abs (downKept / inEnv - 1.0) < 0.1, msg);
    CHECK_MSG (upChipmunk > 1.2 * inEnv, msg);                 // off: envelope follows pitch up...
    CHECK_MSG (downMonster < 0.8 * inEnv, msg);                // ...and down
    // Throat relative to the same engine at 100% (Character itself slightly smooths the envelope).
    CHECK_MSG (throatLong < 0.8 * throatNeutral, msg);         // longer tract: darker  (ideal 0.71x)
    CHECK_MSG (throatShort > 1.12 * throatNeutral, msg);       // shorter tract: brighter (ideal 1.28x, we reach ~1.15x)
}

TEST_CASE ("character: clean is spectrally transparent, hard is brighter")
{
    const double fs = 48000.0;
    const auto in = tools::synthVoice (fs, 2.0, tools::constantPitch (196.0));
    const std::array<double, 5> edges { 50.0, 1000.0, 3000.0, 6000.0, 10000.0 };
    const auto ref = test::bandEnergies<4> (in.samples, fs, edges);

    auto tilt = [&] (float character) {
        dsp::EngineParams p;
        p.correction.noteModes.fill (dsp::NoteMode::bypassed);
        p.shifter.character = character;
        const auto e = test::bandEnergies<4> (test::renderEngine (in.samples, fs, p), fs, edges);
        std::array<double, 4> db {};
        for (size_t b = 0; b < 4; ++b)
            db[b] = 10.0 * std::log10 (e[b] / ref[b]);
        return db;
    };

    const auto clean = tilt (0.0f), hard = tilt (1.0f);
    std::string msg = "clean";
    for (double d : clean) msg += " " + std::to_string (d);
    msg += " | hard";
    for (double d : hard) msg += " " + std::to_string (d);

    for (double d : clean)
        CHECK_MSG (std::abs (d) < 1.0, msg);
    CHECK_MSG (hard[3] - hard[0] > 2.0, msg); // 6-10 kHz lifted relative to the low band
    CHECK_MSG (hard[3] > clean[3] + 1.5, msg);
}
