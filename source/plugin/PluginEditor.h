#pragma once

#include "PluginProcessor.h"
#include "ui/EvoxLookAndFeel.h"
#include "ui/Knob.h"
#include "ui/NoteKeyboard.h"
#include "ui/Panels.h"
#include "ui/PitchDisplay.h"

namespace evox
{

/** EVOX editor. Vector-drawn and resizable (default 980x560, min 760x440).
    A single 60 Hz timer drains the analysis FIFO and level meters and repaints only the
    live components; nothing here ever blocks or waits on the audio thread. */
class EvoxEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EvoxEditor (EvoxAudioProcessor&);
    ~EvoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Runs one UI tick immediately (used by the offline snapshot tool). */
    void tickForSnapshot() { timerCallback(); }
    void setVibratoExpanded (bool e) { vibrato_.setExpanded (e); }

    static constexpr int defaultWidth = 980, defaultHeight = 560, minWidth = 760, minHeight = 440;

private:
    void timerCallback() override;
    void layoutRightColumn (juce::Rectangle<int>);

    EvoxAudioProcessor& processor_;
    ui::EvoxLookAndFeel lookAndFeel_;
    juce::TooltipWindow tooltips_ { this, 600 };

    ui::TopBar topBar_;
    ui::CorrectionMeter correctionMeter_;
    ui::PitchDisplay pitchDisplay_;

    // Left column
    juce::ComboBox key_, scale_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment_, scaleAttachment_;
    ui::NoteKeyboard keyboard_;
    ui::ValueBar reference_;

    // Main control row
    ui::Knob retune_, humanize_, tracking_, character_, throat_, naturalVibrato_, mix_;

    // Right column
    ui::LevelMeter inMeter_ { "In" }, outMeter_ { "Out" };
    ui::Knob outputGain_;
    juce::ToggleButton formant_ { "Formant" }, lowCut_ { "Low Cut" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> formantAttachment_, lowCutAttachment_;
    ui::ValueBar transpose_, detune_;
    ui::VibratoPanel vibrato_;

    juce::Rectangle<int> leftPanel_, rowPanel_, rightPanel_;
    int tickCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EvoxEditor)
};

} // namespace evox
