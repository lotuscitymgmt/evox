#include "Parameters.h"

namespace evox
{

juce::String ParamID::note (int pitchClass)
{
    return "note" + juce::String (pitchClass);
}

namespace
{
constexpr int version = 1;

juce::ParameterID pid (const juce::String& id) { return { id, version }; }

auto floatAttrs (const juce::String& suffix, int decimals)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel (suffix)
        .withStringFromValueFunction ([decimals, suffix] (float v, int) {
            return juce::String (v, decimals) + (suffix.isEmpty() ? "" : " " + suffix);
        });
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterBool> (pid (ParamID::bypass), "Bypass", false));

    // --- Input ---------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (ParamID::inputType), "Input Type",
        StringArray { "Soprano", "Alto/Tenor", "Low Male", "Instrument" }, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::tracking), "Tracking", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        floatAttrs ("", 0)));

    layout.add (std::make_unique<AudioParameterBool> (pid (ParamID::lowCut), "Low Cut 60Hz", true));

    // --- Scale ---------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterChoice> (pid (ParamID::key), "Key", noteNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (ParamID::scale), "Scale", StringArray { "Chromatic", "Major", "Minor" }, 0));

    for (int pc = 0; pc < 12; ++pc)
        layout.add (std::make_unique<AudioParameterChoice> (
            pid (ParamID::note (pc)), "Note " + noteNames()[pc],
            StringArray { "Scale", "Enabled", "Removed", "Bypass" }, 0));

    // --- Correction ----------------------------------------------------------
    NormalisableRange<float> retuneRange (0.0f, 400.0f, 0.1f);
    retuneRange.setSkewForCentre (50.0f); // resolution where it matters: 0-50 ms
    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::retuneSpeed), "Retune Speed", retuneRange, 0.0f, floatAttrs ("ms", 1)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::humanize), "Humanize", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        floatAttrs ("", 0)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::naturalVibrato), "Natural Vibrato", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        floatAttrs ("dB", 1)));

    layout.add (std::make_unique<AudioParameterInt> (
        pid (ParamID::transpose), "Transpose", -24, 24, 0,
        AudioParameterIntAttributes().withLabel ("st")));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::detune), "Detune", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        floatAttrs ("ct", 1)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::referencePitch), "Reference Pitch", NormalisableRange<float> (415.0f, 465.0f, 0.1f), 440.0f,
        floatAttrs ("Hz", 1)));

    // --- Create Vibrato ------------------------------------------------------
    layout.add (std::make_unique<AudioParameterBool> (pid (ParamID::vibratoOn), "Vibrato On", false));
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (ParamID::vibratoShape), "Vibrato Shape", StringArray { "Sine", "Square", "Saw" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::vibratoRate), "Vibrato Rate", NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.6f), 5.5f,
        floatAttrs ("Hz", 2)));
    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::vibratoDepth), "Vibrato Depth", NormalisableRange<float> (0.0f, 200.0f, 0.1f), 30.0f,
        floatAttrs ("ct", 0)));
    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::vibratoDelay), "Vibrato Delay", NormalisableRange<float> (0.0f, 2000.0f, 1.0f), 300.0f,
        floatAttrs ("ms", 0)));

    // --- Character / formant -------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::character), "Character", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f,
        floatAttrs ("", 0)));
    layout.add (std::make_unique<AudioParameterBool> (pid (ParamID::formantOn), "Formant Correction", true));
    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::throat), "Throat", NormalisableRange<float> (50.0f, 180.0f, 0.1f), 100.0f,
        floatAttrs ("%", 0)));

    // --- Output --------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::mix), "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, floatAttrs ("%", 0)));
    layout.add (std::make_unique<AudioParameterFloat> (
        pid (ParamID::outputGain), "Output Gain", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        floatAttrs ("dB", 1)));
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (ParamID::oversampling), "Oversampling", StringArray { "1x", "2x" }, 0));

    return layout;
}

} // namespace evox
