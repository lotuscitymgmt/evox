#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace evox::ui
{

/** Rotary knob behaviour per spec:
      - drag vertically (250 px for full travel), hold Shift for 10x finer control
      - double-click resets to the parameter default
      - mouse wheel adjusts; value bubble on hover and while dragging
      - the value box under the knob is click-to-edit (type a number + Enter)
    Host gestures still come from juce::Slider (mouseDown/mouseUp), so automation
    recording and touch modes work; only the drag maths is ours. */
class EvoxSlider : public juce::Slider
{
public:
    EvoxSlider();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    float lastY_ = 0.0f;
};

/** Caption + knob + editable value, bound to one APVTS parameter. */
class Knob : public juce::Component
{
public:
    enum class Size
    {
        hero,
        medium,
        small
    };

    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, Size,
          const juce::String& tooltip = {});

    /** Arc grows from this plain value instead of the minimum (e.g. 0 dB, 100 %). */
    void setBipolarCentre (float plainValue);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider& getSlider() noexcept { return slider_; }
    const juce::String& getCaption() const noexcept { return caption_; }
    /** Shared caption height so a row of knobs uses one consistent size. */
    void setCaptionHeight (float h) { captionHeight_ = h; repaint(); }

private:
    EvoxSlider slider_;
    juce::String caption_;
    Size size_;
    float captionHeight_ = 0.0f;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

/** Compact horizontal value bar (drag/click-to-edit), for secondary numeric parameters. */
class ValueBar : public juce::Component
{
public:
    ValueBar (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption,
              const juce::String& tooltip = {});
    void setBipolarCentre (float plainValue);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    EvoxSlider slider_;
    juce::String caption_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

} // namespace evox::ui
