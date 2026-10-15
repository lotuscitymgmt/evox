#include "PluginEditor.h"

namespace evox
{

using ui::Knob;

EvoxEditor::EvoxEditor (EvoxAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor_ (p),
      topBar_ (p),
      keyboard_ (p.getState()),
      reference_ (p.getState(), ParamID::referencePitch, "A4 Ref", "Reference pitch for A4 (415-465 Hz)"),
      retune_ (p.getState(), ParamID::retuneSpeed, "Retune Speed", Knob::Size::hero,
               "How fast pitch is pulled to the target. 0 ms = instant, stepped hard-tune."),
      humanize_ (p.getState(), ParamID::humanize, "Humanize", Knob::Size::medium,
                 "Slows the retune on sustained notes; short notes still snap."),
      tracking_ (p.getState(), ParamID::tracking, "Tracking", Knob::Size::medium,
                 "Low = tight (rejects octave errors). High = loose (follows rough/breathy voices)."),
      character_ (p.getState(), ParamID::character, "Character", Knob::Size::medium,
                  "Clean (smooth grains) to hard (short, peaky grains: the glassy edge)."),
      throat_ (p.getState(), ParamID::throat, "Throat", Knob::Size::medium,
               "Vocal-tract length: above 100% darker, below brighter. Independent of pitch."),
      naturalVibrato_ (p.getState(), ParamID::naturalVibrato, "Nat. Vibrato", Knob::Size::medium,
                       "Scales the singer's own vibrato, independent of the retune speed."),
      mix_ (p.getState(), ParamID::mix, "Mix", Knob::Size::medium, "Dry / wet"),
      outputGain_ (p.getState(), ParamID::outputGain, "Output", Knob::Size::small, "Output gain"),
      transpose_ (p.getState(), ParamID::transpose, "Transpose", "Shift the corrected pitch (semitones)"),
      detune_ (p.getState(), ParamID::detune, "Detune", "Offset the tuning grid (cents)"),
      vibrato_ (p.getState())
{
    setLookAndFeel (&lookAndFeel_);
    auto& state = p.getState();

    for (auto* c : std::initializer_list<juce::Component*> {
             &topBar_, &correctionMeter_, &pitchDisplay_, &key_, &scale_, &keyboard_, &reference_, &retune_, &humanize_,
             &tracking_, &character_, &throat_, &naturalVibrato_, &mix_, &inMeter_, &outMeter_, &outputGain_, &formant_,
             &lowCut_, &transpose_, &detune_, &vibrato_ })
        addAndMakeVisible (c);

    key_.addItemList (noteNames(), 1);
    key_.setTooltip ("Key");
    scale_.addItemList ({ "Chromatic", "Major", "Minor" }, 1);
    scale_.setTooltip ("Scale");
    keyAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ParamID::key, key_);
    scaleAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ParamID::scale, scale_);

    formant_.setTooltip ("Formant correction: keeps shifted vocals from sounding chipmunked");
    lowCut_.setTooltip ("60 Hz high-pass on the input");
    formantAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ParamID::formantOn, formant_);
    lowCutAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ParamID::lowCut, lowCut_);

    throat_.setBipolarCentre (100.0f);
    naturalVibrato_.setBipolarCentre (0.0f);
    transpose_.setBipolarCentre (0.0f);
    detune_.setBipolarCentre (0.0f);
    reference_.setBipolarCentre (440.0f);

    vibrato_.onToggle = [this] { resized(); };

    // Children were built before they had a parent using our LookAndFeel; components that
    // cache colours at lookAndFeelChanged() time (slider text boxes) must be refreshed.
    sendLookAndFeelChange();

    // Restore the last window size (stored in the plugin state).
    const int w = state.state.getProperty ("uiWidth", defaultWidth);
    const int h = state.state.getProperty ("uiHeight", defaultHeight);
    setResizable (true, true);
    setResizeLimits (minWidth, minHeight, 2400, 1400);
    setSize (juce::jlimit (minWidth, 2400, w), juce::jlimit (minHeight, 1400, h));

    startTimerHz (60);
}

EvoxEditor::~EvoxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EvoxEditor::timerCallback()
{
    const double fs = processor_.getEngine().getSampleRate();
    const auto ref = processor_.getState().getRawParameterValue (ParamID::referencePitch)->load();
    pitchDisplay_.setReferencePitch (ref);

    // Drain the lock-free analysis FIFO.
    dsp::AnalysisFrame frame;
    bool any = false;
    while (processor_.popAnalysisFrame (frame))
    {
        pitchDisplay_.addFrame (frame, fs);
        any = true;
    }

    if (any)
    {
        correctionMeter_.setCents (frame.correctionCents, frame.voiced && frame.targetMidi > 0.0f);
        keyboard_.setCurrentTarget (frame.targetMidi > 0.0f ? ((juce::roundToInt (frame.targetMidi) % 12) + 12) % 12 : -1);
    }
    else
    {
        correctionMeter_.setCents (0.0f, false);
    }

    pitchDisplay_.tick (juce::Time::getMillisecondCounterHiRes() * 0.001);

    auto& meters = processor_.getEngine().getMeters();
    inMeter_.push (meters.inPeak.exchange (0.0f), meters.inRms.load());
    outMeter_.push (meters.outPeak.exchange (0.0f), meters.outRms.load());

    keyboard_.refresh();
    pitchDisplay_.setNoteStates (keyboard_.getTargets());

    if (++tickCount_ % 6 == 0) // ~10 Hz is plenty for preset name / latency / A-B
        topBar_.refresh();
}

void EvoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::Colours::background);
    ui::drawPanel (g, leftPanel_.toFloat(), 9.0f);
    ui::drawPanel (g, rowPanel_.toFloat(), 9.0f);
    ui::drawPanel (g, rightPanel_.toFloat(), 9.0f);

    auto left = leftPanel_.reduced (12, 10).toFloat();
    ui::drawCaption (g, "Key", left.removeFromTop (14.0f));
    left.removeFromTop (34.0f);
    ui::drawCaption (g, "Scale", left.removeFromTop (14.0f));
    left.removeFromTop (34.0f);
    ui::drawCaption (g, "Notes", left.removeFromTop (14.0f));

    // Divider between the hero knob and the medium knobs.
    g.setColour (ui::Colours::line);
    const int dividerX = retune_.getRight() + 6;
    g.drawVerticalLine (dividerX, float (rowPanel_.getY() + 14), float (rowPanel_.getBottom() - 14));
}

void EvoxEditor::resized()
{
    auto& state = processor_.getState().state;
    state.setProperty ("uiWidth", getWidth(), nullptr);
    state.setProperty ("uiHeight", getHeight(), nullptr);

    auto b = getLocalBounds().reduced (10);
    topBar_.setBounds (b.removeFromTop (50));
    b.removeFromTop (10);

    const int leftWidth = juce::jlimit (128, 180, getWidth() * 15 / 100);
    const int rightWidth = juce::jlimit (176, 230, getWidth() * 19 / 100);

    // Left column: Key, Scale, keyboard, reference pitch.
    leftPanel_ = b.removeFromLeft (leftWidth);
    {
        auto c = leftPanel_.reduced (12, 10);
        c.removeFromTop (14);
        key_.setBounds (c.removeFromTop (28));
        c.removeFromTop (6 + 14);
        scale_.setBounds (c.removeFromTop (28));
        c.removeFromTop (6 + 14);
        reference_.setBounds (c.removeFromBottom (22));
        c.removeFromBottom (8);
        keyboard_.setBounds (c);
    }
    b.removeFromLeft (10);

    rightPanel_ = b.removeFromRight (rightWidth);
    layoutRightColumn (rightPanel_);
    b.removeFromRight (10);

    // Centre: correction meter, pitch display, control row.
    correctionMeter_.setBounds (b.removeFromTop (30));
    b.removeFromTop (8);
    const int rowHeight = juce::jlimit (124, 190, getHeight() * 28 / 100);
    rowPanel_ = b.removeFromBottom (rowHeight);
    b.removeFromBottom (8);
    pitchDisplay_.setBounds (b);

    auto row = rowPanel_.reduced (10, 8);
    retune_.setBounds (row.removeFromLeft (juce::jmin (row.getHeight() + 30, row.getWidth() * 21 / 100)));
    row.removeFromLeft (12);
    const int knobWidth = row.getWidth() / 6;
    auto medium = [&] (Knob& k) { k.setBounds (row.removeFromLeft (knobWidth).withTrimmedTop (row.getHeight() / 8)); };
    medium (humanize_);
    medium (tracking_);
    medium (character_);
    medium (throat_);
    medium (naturalVibrato_);
    mix_.setBounds (row.withTrimmedTop (row.getHeight() / 8));

    float captionHeight = 10.5f;
    for (auto* k : { &humanize_, &tracking_, &character_, &throat_, &naturalVibrato_, &mix_ })
        captionHeight = juce::jmin (captionHeight, ui::captionHeightToFit (k->getCaption(), float (k->getWidth() - 4)));
    for (auto* k : { &humanize_, &tracking_, &character_, &throat_, &naturalVibrato_, &mix_ })
        k->setCaptionHeight (captionHeight);
}

void EvoxEditor::layoutRightColumn (juce::Rectangle<int> area)
{
    auto c = area.reduced (10, 10);

    // Vibrato panel docks at the bottom; when expanded it overlays the upper sections.
    const int vibratoHeight = vibrato_.isExpanded() ? juce::jmin (ui::VibratoPanel::expandedHeight, c.getHeight())
                                                    : ui::VibratoPanel::headerHeight;
    vibrato_.setBounds (c.withTop (c.getBottom() - vibratoHeight));
    vibrato_.toFront (false);
    c.removeFromBottom (ui::VibratoPanel::headerHeight + 8);

    // Meters + output gain.
    auto meters = c.removeFromTop (juce::jmax (110, c.getHeight() - 4 * 28 - 8));
    const int meterWidth = meters.getWidth() / 4;
    inMeter_.setBounds (meters.removeFromLeft (meterWidth));
    outMeter_.setBounds (meters.removeFromLeft (meterWidth));
    outputGain_.setBounds (meters.withSizeKeepingCentre (meters.getWidth(), juce::jmin (meters.getHeight(), 110)));
    c.removeFromTop (8);

    formant_.setBounds (c.removeFromTop (28));
    lowCut_.setBounds (c.removeFromTop (28));
    transpose_.setBounds (c.removeFromTop (28).reduced (0, 2));
    detune_.setBounds (c.removeFromTop (28).reduced (0, 2));
}

} // namespace evox
