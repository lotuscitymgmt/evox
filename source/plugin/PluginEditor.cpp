#include "PluginEditor.h"

#include <cmath>

namespace evox
{

namespace
{
const juce::Colour background { 0xff0e0f12 };
const juce::Colour panel { 0xff17191e };
const juce::Colour accent { 0xffe5487f };
const juce::Colour cyan { 0xff3fc1c9 };
constexpr int headerHeight = 72;
} // namespace

EvoxEditor::EvoxEditor (EvoxAudioProcessor& p)
    : AudioProcessorEditor (p), processor_ (p), generic_ (p)
{
    addAndMakeVisible (generic_);
    setResizable (true, true);
    setResizeLimits (760, 440, 1600, 1200);
    setSize (980, 560);
    startTimerHz (30);
}

EvoxEditor::~EvoxEditor()
{
    stopTimer();
}

void EvoxEditor::timerCallback()
{
    dsp::AnalysisFrame frame;
    bool got = false;
    while (processor_.popAnalysisFrame (frame))
        got = true;

    if (got)
    {
        latest_ = frame;
        haveFrame_ = true;
        repaint (0, 0, getWidth(), headerHeight);
    }
}

void EvoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (10, 8);
    g.setColour (panel);
    g.fillRoundedRectangle (header.toFloat(), 9.0f);
    header.reduce (14, 4);

    g.setColour (accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("EVOX", header.removeFromLeft (110), juce::Justification::centredLeft);

    const auto& engine = processor_.getEngine();
    const auto latencyMs = 1000.0 * processor_.getLatencySamples() / engine.getSampleRate();

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (engine.getSampleRate() / 1000.0, 1) + " kHz  |  latency "
                    + juce::String (processor_.getLatencySamples()) + " smp (" + juce::String (latencyMs, 1) + " ms)",
                header.removeFromRight (280), juce::Justification::centredRight);

    const auto ref = processor_.getState().getRawParameterValue (ParamID::referencePitch)->load();
    auto noteText = [ref] (float hz) {
        const auto midi = dsp::hzToMidi (hz, ref);
        const auto nearest = static_cast<int> (std::lround (midi));
        const auto cents = 100.0f * (midi - static_cast<float> (nearest));
        return noteNames()[((nearest % 12) + 12) % 12] + juce::String (nearest / 12 - 1) + " "
             + (cents >= 0 ? "+" : "") + juce::String (cents, 0) + "ct";
    };

    juce::String readout = "--", corrected;
    if (haveFrame_ && latest_.voiced && latest_.inputHz > 0.0f)
    {
        readout = "in " + noteText (latest_.inputHz) + "  " + juce::String (latest_.inputHz, 1) + " Hz";
        corrected = "out " + noteText (latest_.outputHz) + "   corr " + (latest_.correctionCents >= 0 ? "+" : "")
                  + juce::String (latest_.correctionCents, 0) + " ct";
    }
    else if (haveFrame_)
    {
        readout = "unvoiced";
    }

    g.setColour (accent);
    g.setFont (juce::FontOptions (17.0f));
    g.drawText (corrected, header.withTrimmedLeft (300), juce::Justification::centredLeft);

    g.setColour (cyan);
    g.setFont (juce::FontOptions (17.0f));
    g.drawText (readout, header.withWidth (300), juce::Justification::centredLeft);
}

void EvoxEditor::resized()
{
    generic_.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
}

} // namespace evox
