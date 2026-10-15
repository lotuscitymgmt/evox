#include "EvoxLookAndFeel.h"

namespace evox::ui
{

EvoxLookAndFeel::EvoxLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Colours::accent.withAlpha (0.4f));
    setColour (juce::ComboBox::backgroundColourId, Colours::panelRaised);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::line);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panelRaised);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);
    setColour (juce::TextButton::buttonColourId, Colours::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::textDim);
    setColour (juce::TextButton::textColourOnId, Colours::text);
    setColour (juce::TextEditor::backgroundColourId, Colours::background);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::highlightColourId, Colours::accent.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, Colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    setColour (juce::CaretComponent::caretColourId, Colours::accent);
    setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::line);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panelRaised);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::BubbleComponent::backgroundColourId, Colours::panelRaised);
    setColour (juce::BubbleComponent::outlineColourId, Colours::lineStrong);
}

juce::Typeface::Ptr EvoxLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    return typeface (f.isBold() ? Weight::semiBold : Weight::regular);
}

// --- Sliders ---------------------------------------------------------------------

void EvoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                        float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> (float (x), float (y), float (w), float (h)).reduced (2.0f);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float radius = size * 0.5f;
    const float track = juce::jmax (2.5f, size * 0.055f);
    const float arcRadius = radius - track * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool enabled = slider.isEnabled();
    const bool hover = slider.isMouseOverOrDragging();

    // Background track
    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::line);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc: from the bipolar centre if the slider asks for one, otherwise from the start.
    float fromAngle = startAngle;
    const auto bipolar = slider.getProperties()["bipolarCentre"];
    if (! bipolar.isVoid())
        fromAngle = startAngle + float (slider.valueToProportionOfLength (double (bipolar))) * (endAngle - startAngle);

    if (std::abs (angle - fromAngle) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (fromAngle, angle),
                           juce::jmax (fromAngle, angle), true);
        const auto c = enabled ? Colours::accent : Colours::textFaint;
        g.setColour (c.withAlpha (0.25f));
        g.strokePath (arc, juce::PathStrokeType (track * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (arc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knob body: flat disc, soft top light, inner shadow edge.
    const float bodyRadius = arcRadius - track * 1.6f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (Colours::panelRaised.brighter (hover ? 0.12f : 0.06f), centre.x, body.getY(),
                                             Colours::panel, centre.x, body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // Pointer
    const float pointerLength = bodyRadius * 0.62f;
    const juce::Point<float> tip (centre.x + std::sin (angle) * (bodyRadius - track * 0.6f),
                                  centre.y - std::cos (angle) * (bodyRadius - track * 0.6f));
    const juce::Point<float> tail (centre.x + std::sin (angle) * (bodyRadius - pointerLength),
                                   centre.y - std::cos (angle) * (bodyRadius - pointerLength));
    g.setColour (enabled ? Colours::text : Colours::textFaint);
    g.drawLine ({ tail, tip }, juce::jmax (2.0f, size * 0.035f));
}

void EvoxLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, slider);
        return;
    }

    // Compact value bar (Transpose / Detune / Reference): fill from the bipolar centre.
    auto b = juce::Rectangle<float> (float (x), float (y), float (w), float (h));
    g.setColour (Colours::background);
    g.fillRoundedRectangle (b, 5.0f);

    float from = b.getX();
    const auto bipolar = slider.getProperties()["bipolarCentre"];
    if (! bipolar.isVoid())
        from = b.getX() + float (slider.valueToProportionOfLength (double (bipolar))) * b.getWidth();

    auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, pos), b.getY(), juce::jmax (from, pos), b.getBottom());
    g.setColour (Colours::accent.withAlpha (slider.isMouseOverOrDragging() ? 0.45f : 0.32f));
    g.fillRoundedRectangle (fill, 5.0f);
    g.setColour (slider.isMouseOverOrDragging() ? Colours::lineStrong : Colours::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
}

juce::Label* EvoxLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (font (slider.getProperties()["hero"] ? 17.0f : 12.5f, Weight::medium));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);
    l->setColour (juce::Label::backgroundWhenEditingColourId, Colours::background);
    l->setColour (juce::Label::textWhenEditingColourId, Colours::text);
    l->setMinimumHorizontalScale (0.8f);
    return l;
}

juce::Slider::SliderLayout EvoxLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearBar)
    {
        juce::Slider::SliderLayout layout;
        layout.sliderBounds = slider.getLocalBounds();
        layout.textBoxBounds = slider.getLocalBounds();
        return layout;
    }
    return LookAndFeel_V4::getSliderLayout (slider);
}

juce::Font EvoxLookAndFeel::getSliderPopupFont (juce::Slider&)
{
    return font (12.5f, Weight::medium);
}

void EvoxLookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&,
                                  const juce::Rectangle<float>& body)
{
    g.setColour (Colours::panelRaised);
    g.fillRoundedRectangle (body, 6.0f);
    g.setColour (Colours::lineStrong);
    g.drawRoundedRectangle (body.reduced (0.5f), 6.0f, 1.0f);
}

void EvoxLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto b = juce::Rectangle<float> (float (w), float (h));
    g.setColour (Colours::panelRaised);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (Colours::lineStrong);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (Colours::text);
    g.setFont (font (12.0f));
    g.drawFittedText (text, b.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> EvoxLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (font (12.0f), text, 0, 0, 260.0f, 200.0f, juce::Justification::topLeft, 4);
    const auto textBounds = ga.getBoundingBox (0, -1, true);
    const int w = juce::roundToInt (textBounds.getWidth()) + 18, h = juce::roundToInt (textBounds.getHeight()) + 10;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 14, w, h)
        .constrainedWithin (parent);
}

// --- Combo / popup -----------------------------------------------------------------

void EvoxLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto b = juce::Rectangle<float> (float (w), float (h));
    g.setColour (box.isMouseOver (true) ? Colours::panelRaised.brighter (0.06f) : Colours::panelRaised);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Colours::accent.withAlpha (0.6f) : Colours::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

    juce::Path arrow;
    const float ax = b.getRight() - 14.0f, ay = b.getCentreY();
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (Colours::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font EvoxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jmin (13.5f, float (box.getHeight()) * 0.5f), Weight::medium);
}

void EvoxLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // One line only: the label height decides how many lines JUCE will wrap to.
    const auto f = getComboBoxFont (box);
    const int h = juce::roundToInt (f.getHeight() * 1.5f);
    label.setBounds (8, (box.getHeight() - h) / 2, box.getWidth() - 26, h);
    label.setFont (f);
    label.setMinimumHorizontalScale (0.75f);
}

juce::Font EvoxLookAndFeel::getPopupMenuFont()
{
    return font (13.5f);
}

void EvoxLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Colours::panelRaised);
    g.setColour (Colours::lineStrong);
    g.drawRect (0, 0, w, h);
}

// --- Buttons ------------------------------------------------------------------------

void EvoxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? Colours::accent.withAlpha (0.22f) : Colours::panelRaised;
    if (over)
        fill = fill.brighter (0.08f);
    if (down)
        fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? Colours::accent.withAlpha (0.8f) : Colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

juce::Font EvoxLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (juce::jmin (13.0f, float (h) * 0.5f), Weight::semiBold);
}

void EvoxLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? Colours::text : (b.isEnabled() ? Colours::textDim : Colours::textFaint));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void EvoxLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    // Pill switch + label.
    const auto r = b.getLocalBounds().toFloat();
    const float h = juce::jmin (16.0f, r.getHeight() - 4.0f), w = h * 1.8f;
    const auto pill = juce::Rectangle<float> (w, h).withCentre ({ r.getRight() - w * 0.5f - 2.0f, r.getCentreY() });
    const bool on = b.getToggleState();

    g.setColour (on ? Colours::accent : Colours::line.brighter (over ? 0.15f : 0.0f));
    g.fillRoundedRectangle (pill, h * 0.5f);
    const float knob = h - 4.0f;
    g.setColour (on ? Colours::text : Colours::textDim);
    g.fillEllipse (juce::Rectangle<float> (knob, knob).withCentre ({ on ? pill.getRight() - h * 0.5f : pill.getX() + h * 0.5f, pill.getCentreY() }));

    g.setColour (on ? Colours::text : Colours::textDim);
    g.setFont (font (12.0f, Weight::medium));
    g.drawText (b.getButtonText(), r.withRight (pill.getX() - 6.0f), juce::Justification::centredLeft);
}

void EvoxLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (Colours::background);
    g.fillRoundedRectangle (juce::Rectangle<float> (float (w), float (h)), 5.0f);
}

void EvoxLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? Colours::accent : Colours::line);
    g.drawRoundedRectangle (juce::Rectangle<float> (float (w), float (h)).reduced (0.5f), 5.0f, 1.0f);
}

} // namespace evox::ui
