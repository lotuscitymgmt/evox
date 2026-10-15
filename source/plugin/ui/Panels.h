#pragma once

#include "Knob.h"

namespace evox
{
class EvoxAudioProcessor;
}

namespace evox::ui
{

/** Vector power icon; lit (pink) while EVOX is processing, dim when bypassed. */
class PowerButton final : public juce::Button
{
public:
    PowerButton() : juce::Button ("Bypass") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** Logo, bypass, preset browser, Input Type, sample-rate/latency readout, A/B compare. */
class TopBar final : public juce::Component
{
public:
    explicit TopBar (EvoxAudioProcessor&);

    void refresh(); // preset name / latency / A-B state; called from the UI timer
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuildPresetList();
    void showSaveDialog();
    static juce::Path lotusPath (juce::Rectangle<float>);

    EvoxAudioProcessor& processor_;
    PowerButton power_;
    juce::TextButton prev_ { "<" }, next_ { ">" }, save_ { "Save" }, slotA_ { "A" }, slotB_ { "B" }, copy_ { "A>B" };
    juce::ComboBox presets_, inputType_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inputTypeAttachment_;
    juce::Rectangle<int> logoArea_, infoArea_;
    juce::String shownPreset_;
    juce::String infoText_;
    std::unique_ptr<juce::AlertWindow> saveDialog_;
};

/** Collapsible "Create Vibrato" section: on/off, shape, rate, depth, onset delay. */
class VibratoPanel final : public juce::Component
{
public:
    explicit VibratoPanel (juce::AudioProcessorValueTreeState&);

    bool isExpanded() const noexcept { return expanded_; }
    void setExpanded (bool shouldBeExpanded);
    std::function<void()> onToggle;

    static constexpr int headerHeight = 30;
    static constexpr int expandedHeight = 178;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool expanded_ = false;
    juce::ToggleButton on_ { "" };
    juce::ComboBox shape_;
    Knob rate_, depth_, delay_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAttachment_;
};

} // namespace evox::ui
