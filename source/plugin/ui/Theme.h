#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace evox::ui
{

/** EVOX palette. One accent (lotus pink) plus cyan reserved for *detected* pitch. */
namespace Colours
{
    inline const juce::Colour background { 0xff0e0f12 };
    inline const juce::Colour panel { 0xff17191e };
    inline const juce::Colour panelRaised { 0xff1d2026 };
    inline const juce::Colour line { 0xff262931 };
    inline const juce::Colour lineStrong { 0xff343842 };
    inline const juce::Colour text { 0xffe8e9ec };
    inline const juce::Colour textDim { 0xff8a8f98 };
    inline const juce::Colour textFaint { 0xff5a5f68 };
    inline const juce::Colour accent { 0xffe5487f };  // lotus pink: corrected output, active controls
    inline const juce::Colour cyan { 0xff3fc1c9 };    // detected input pitch
    inline const juce::Colour warn { 0xfff2b84b };
}

enum class Weight
{
    regular,
    medium,
    semiBold,
    bold
};

/** Inter (embedded, OFL) with tabular numerals so changing values never jitter. */
juce::Font font (float height, Weight weight = Weight::regular);
juce::Typeface::Ptr typeface (Weight weight);

/** Rounded panel with a subtle inner shadow and top highlight (no skeuomorphic metal). */
void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, float radius = 9.0f, juce::Colour fill = Colours::panel);

/** Small-caps section caption. */
void drawCaption (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                  juce::Justification just = juce::Justification::centredLeft, float height = 0.0f);

/** Caption font, and the largest caption height (<= 10.5) at which `text` fits `width`. */
juce::Font captionFont (float height);
float captionHeightToFit (const juce::String& text, float width);

} // namespace evox::ui
