#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace evox
{

/** Stable parameter IDs. Never rename an ID once shipped: hosts store automation by ID. */
namespace ParamID
{
    inline constexpr auto bypass          = "bypass";
    inline constexpr auto inputType       = "inputType";
    inline constexpr auto tracking        = "tracking";
    inline constexpr auto lowCut          = "lowCut";
    inline constexpr auto key             = "key";
    inline constexpr auto scale           = "scale";
    inline constexpr auto retuneSpeed     = "retuneSpeed";
    inline constexpr auto humanize        = "humanize";
    inline constexpr auto naturalVibrato  = "naturalVibrato";
    inline constexpr auto vibratoOn       = "vibratoOn";
    inline constexpr auto vibratoShape    = "vibratoShape";
    inline constexpr auto vibratoRate     = "vibratoRate";
    inline constexpr auto vibratoDepth    = "vibratoDepth";
    inline constexpr auto vibratoDelay    = "vibratoDelay";
    inline constexpr auto transpose       = "transpose";
    inline constexpr auto detune          = "detune";
    inline constexpr auto referencePitch  = "referencePitch";
    inline constexpr auto character       = "character";
    inline constexpr auto formantOn       = "formantOn";
    inline constexpr auto throat          = "throat";
    inline constexpr auto mix             = "mix";
    inline constexpr auto outputGain      = "outputGain";
    inline constexpr auto oversampling    = "oversampling";

    /** Per-note state for the 12 pitch classes: "note0" (C) .. "note11" (B). */
    juce::String note (int pitchClass);
}

/** Per-note override. "Scale" follows Key + Scale; the rest force a state. */
enum class NoteState
{
    scale = 0,
    enabled,
    removed,
    bypassed
};

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

inline const juce::StringArray& noteNames()
{
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names;
}

} // namespace evox
