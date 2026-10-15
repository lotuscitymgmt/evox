#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace evox::ui
{

/** Vertical one-octave keyboard (C at the bottom, B at the top).
    Each key shows the note's *effective* state, by colour:
      pink   = enabled target        dark/struck = removed (never a target)
      cyan   = bypassed (left alone)  a small dot = explicitly overridden (not following the scale)
    Click cycles Enabled -> Removed -> Bypass; right-click (or Alt-click) returns the key to
    "follow Key/Scale". The note currently being targeted is outlined. */
class NoteKeyboard final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit NoteKeyboard (juce::AudioProcessorValueTreeState&);

    void setCurrentTarget (int pitchClass) noexcept;
    /** Polls parameter state (UI timer); repaints only when something changed. */
    void refresh();

    /** Effective "is a correction target" per pitch class, for the pitch display shading. */
    std::array<bool, 12> getTargets() const noexcept;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum class Effective { enabled, removed, bypassed };

    int pitchClassAt (juce::Point<float>) const noexcept;
    juce::Rectangle<float> keyBounds (int pitchClass) const noexcept;
    Effective effectiveState (int pitchClass) const noexcept;
    void setNoteMode (int pitchClass, int mode);

    juce::AudioProcessorValueTreeState& state_;
    std::array<int, 12> modes_ {};
    int key_ = 0, scale_ = 0, target_ = -1, hover_ = -1;
};

/** Vertical peak + RMS meter with peak hold, -60..+6 dBFS. */
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (juce::String caption);
    void push (float peak, float rms) noexcept; // linear, called every UI tick
    void paint (juce::Graphics&) override;

private:
    static float toPos (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); }

    juce::String caption_;
    float peakDb_ = -100.0f, rmsDb_ = -100.0f, holdDb_ = -100.0f;
    int holdFrames_ = 0;
};

} // namespace evox::ui
