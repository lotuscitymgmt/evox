#include "Theme.h"

#include "BinaryData.h"

namespace evox::ui
{

juce::Typeface::Ptr typeface (Weight weight)
{
    static auto regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    static auto medium = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
    static auto semiBold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
    static auto bold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize);

    switch (weight)
    {
        case Weight::medium:   return medium;
        case Weight::semiBold: return semiBold;
        case Weight::bold:     return bold;
        case Weight::regular:  break;
    }
    return regular;
}

juce::Font font (float height, Weight weight)
{
    return juce::Font (juce::FontOptions (typeface (weight))
                           .withHeight (height)
                           .withFeatureEnabled ("tnum"));
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> b, float radius, juce::Colour fill)
{
    g.setColour (fill);
    g.fillRoundedRectangle (b, radius);

    // Inner shadow: a darker stroke just inside the edge, fading downwards.
    juce::Path outline;
    outline.addRoundedRectangle (b.reduced (0.5f), radius);
    g.saveState();
    g.reduceClipRegion (outline);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), b.getX(), b.getY(),
                                             juce::Colours::transparentBlack, b.getX(), b.getY() + 10.0f, false));
    g.strokePath (outline, juce::PathStrokeType (3.0f));
    g.restoreState();

    g.setColour (Colours::line);
    g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);
}

juce::Font captionFont (float height)
{
    return font (height, Weight::semiBold).withExtraKerningFactor (juce::jmap (height, 8.0f, 10.5f, 0.02f, 0.08f));
}

float captionHeightToFit (const juce::String& text, float width)
{
    float h = 10.5f;
    while (h > 8.0f && juce::GlyphArrangement::getStringWidth (captionFont (h), text.toUpperCase()) > width)
        h -= 0.25f;
    return h;
}

void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification just, float height)
{
    // Shrink to fit narrow columns rather than truncating (or use a shared height).
    if (height <= 0.0f)
        height = captionHeightToFit (text, area.getWidth());
    g.setColour (Colours::textDim);
    g.setFont (captionFont (height));
    g.drawText (text.toUpperCase(), area, just, false);
}

} // namespace evox::ui
