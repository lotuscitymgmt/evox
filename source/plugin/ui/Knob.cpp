#include "Knob.h"

#include <numbers>

namespace evox::ui
{

namespace
{
constexpr float pixelsForFullTravel = 250.0f;
constexpr float fineFactor = 0.1f;
constexpr float arcStart = -0.75f * std::numbers::pi_v<float>;
constexpr float arcEnd = 0.75f * std::numbers::pi_v<float>;
} // namespace

EvoxSlider::EvoxSlider()
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setRotaryParameters (arcStart + 2.0f * std::numbers::pi_v<float>, arcEnd + 2.0f * std::numbers::pi_v<float>, true);
    setScrollWheelEnabled (true);
    setPopupDisplayEnabled (true, true, nullptr, 900);
    setTextBoxIsEditable (true);
    setSliderSnapsToMousePosition (false); // value bars drag relatively, never jump to the click
}

void EvoxSlider::mouseDown (const juce::MouseEvent& e)
{
    juce::Slider::mouseDown (e); // starts the host gesture, shows the value bubble
    lastY_ = e.position.y;
}

void EvoxSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
        return;

    // Vertical for knobs; horizontal for value bars.
    const bool horizontal = getSliderStyle() == juce::Slider::LinearBar;
    const float position = horizontal ? -e.position.x : e.position.y;
    const float delta = (lastY_ - position) * (e.mods.isShiftDown() ? fineFactor : 1.0f);
    lastY_ = position;

    const double proportion = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + double (delta / pixelsForFullTravel));
    setValue (proportionOfLengthToValue (proportion), juce::sendNotificationSync);
}

// ------------------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption, Size size,
            const juce::String& tooltip)
    : caption_ (caption), size_ (size)
{
    if (size == Size::hero)
        slider_.getProperties().set ("hero", true);

    slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, size == Size::hero ? 24 : 18);
    slider_.setTooltip (tooltip);
    addAndMakeVisible (slider_);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider_);

    if (auto* p = state.getParameter (paramId))
        slider_.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::setBipolarCentre (float plainValue)
{
    slider_.getProperties().set ("bipolarCentre", plainValue);
    slider_.repaint();
}

void Knob::resized()
{
    auto b = getLocalBounds();
    b.removeFromTop (size_ == Size::hero ? 18 : 16); // caption
    if (size_ == Size::hero)
    {
        b.removeFromBottom (2);
        b.reduce (12, 0); // room for the FAST / NATURAL arc labels
    }
    const int textHeight = size_ == Size::hero ? 24 : 18;
    slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (b.getWidth(), 90), textHeight);
    slider_.setBounds (b);
}

void Knob::paint (juce::Graphics& g)
{
    drawCaption (g, caption_, getLocalBounds().removeFromTop (size_ == Size::hero ? 18 : 16).toFloat(),
                 juce::Justification::centred, captionHeight_);

    if (size_ != Size::hero)
        return;

    // "FAST" / "NATURAL" labels at the two ends of the hero arc, kept clear of the value box.
    const auto knobArea = slider_.getBounds().withTrimmedBottom (24).toFloat();
    const float labelLimit = knobArea.getBottom() - 6.0f;
    const float size = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
    const auto c = knobArea.getCentre();
    const float r = size * 0.5f + 4.0f;
    const auto at = [&] (float angle) { return juce::Point<float> (c.x + std::sin (angle) * r, c.y - std::cos (angle) * r); };

    const auto left = at (arcStart);
    const auto right = at (arcEnd);
    g.setColour (Colours::accent.withAlpha (0.9f));
    // Labels hang just below each arc end; on small knobs they are pushed apart so they
    // never collide, and always stay inside the component.
    const auto fastFont = font (9.5f, Weight::bold).withExtraKerningFactor (0.1f);
    const float fastW = juce::GlyphArrangement::getStringWidth (fastFont, "FAST") + 2.0f;
    const float naturalW = juce::GlyphArrangement::getStringWidth (fastFont, "NATURAL") + 2.0f;
    const float y = juce::jmin (left.y + 4.0f, labelLimit - 12.0f);
    float fastX = left.x - fastW * 0.5f, naturalX = right.x - naturalW * 0.5f;
    const float overlap = (fastX + fastW + 6.0f) - naturalX;
    if (overlap > 0.0f)
    {
        fastX -= overlap * 0.5f;
        naturalX += overlap * 0.5f;
    }
    const auto bounds = getLocalBounds().toFloat();
    const auto fastRect = juce::Rectangle<float> (fastX, y, fastW, 12.0f).constrainedWithin (bounds);
    const auto naturalRect = juce::Rectangle<float> (naturalX, y, naturalW, 12.0f).constrainedWithin (bounds);

    g.setFont (fastFont);
    g.setColour (Colours::accent.withAlpha (0.9f));
    g.drawText ("FAST", fastRect, juce::Justification::centred);
    g.setColour (Colours::textDim);
    g.drawText ("NATURAL", naturalRect, juce::Justification::centred);
}

// ------------------------------------------------------------------------------------

ValueBar::ValueBar (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
                    const juce::String& tooltip)
    : caption_ (caption)
{
    slider_.setSliderStyle (juce::Slider::LinearBar);
    slider_.setPopupDisplayEnabled (false, false, nullptr);
    slider_.setTooltip (tooltip);
    addAndMakeVisible (slider_);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider_);
    if (auto* p = state.getParameter (paramId))
        slider_.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void ValueBar::setBipolarCentre (float plainValue)
{
    slider_.getProperties().set ("bipolarCentre", plainValue);
}

void ValueBar::resized()
{
    auto b = getLocalBounds();
    b.removeFromLeft (juce::jmin (78, b.getWidth() / 2));
    slider_.setBounds (b.reduced (0, 2));
}

void ValueBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawCaption (g, caption_, b.removeFromLeft (juce::jmin (78.0f, b.getWidth() / 2.0f)));
}

} // namespace evox::ui
