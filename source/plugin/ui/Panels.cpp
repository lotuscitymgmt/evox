#include "Panels.h"

#include "../PluginProcessor.h"

namespace evox::ui
{

void PowerButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool bypassed = getToggleState();
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (over ? Colours::panelRaised.brighter (0.08f) : Colours::panelRaised);
    g.fillEllipse (b);
    g.setColour (bypassed ? Colours::line : Colours::accent.withAlpha (0.6f));
    g.drawEllipse (b.reduced (0.5f), 1.0f);

    const auto c = b.getCentre();
    const float r = b.getWidth() * 0.26f;
    juce::Path icon;
    icon.addCentredArc (c.x, c.y, r, r, 0.0f, 0.65f, 2.0f * juce::MathConstants<float>::pi - 0.65f, true);
    icon.startNewSubPath (c.x, c.y - r * 1.25f);
    icon.lineTo (c.x, c.y - r * 0.15f);
    g.setColour (bypassed ? Colours::textFaint : Colours::accent);
    g.strokePath (icon, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

TopBar::TopBar (EvoxAudioProcessor& p) : processor_ (p)
{
    auto& state = p.getState();

    power_.setTooltip ("Bypass (latency-compensated)");
    powerAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ParamID::bypass, power_);
    addAndMakeVisible (power_);

    for (auto* b : { &prev_, &next_, &save_, &slotA_, &slotB_, &copy_ })
        addAndMakeVisible (b);

    prev_.setTooltip ("Previous preset");
    next_.setTooltip ("Next preset");
    save_.setTooltip ("Save the current settings as a user preset");
    slotA_.setTooltip ("Compare slot A");
    slotB_.setTooltip ("Compare slot B");
    copy_.setTooltip ("Copy the active slot to the other one");

    prev_.onClick = [this] { processor_.getPresetManager().loadPrevious(); refresh(); };
    next_.onClick = [this] { processor_.getPresetManager().loadNext(); refresh(); };
    save_.onClick = [this] { showSaveDialog(); };
    slotA_.onClick = [this] { processor_.selectSlot (0); refresh(); };
    slotB_.onClick = [this] { processor_.selectSlot (1); refresh(); };
    copy_.onClick = [this] { processor_.copyActiveSlotToOther(); };

    presets_.setTextWhenNothingSelected ("Preset");
    presets_.onChange = [this] {
        const int index = presets_.getSelectedItemIndex();
        if (index >= 0 && presets_.getText() != processor_.getPresetManager().getCurrentName())
            processor_.getPresetManager().loadPreset (index);
    };
    addAndMakeVisible (presets_);

    inputType_.addItemList ({ "Soprano", "Alto/Tenor", "Low Male", "Instrument" }, 1);
    inputType_.setTooltip ("Input Type: sets the pitch search range and analysis window");
    inputTypeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ParamID::inputType, inputType_);
    addAndMakeVisible (inputType_);

    rebuildPresetList();
    refresh();
}

void TopBar::rebuildPresetList()
{
    auto& pm = processor_.getPresetManager();
    presets_.clear (juce::dontSendNotification);
    const auto names = pm.getPresetNames();
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == pm.getNumFactoryPresets())
            presets_.addSeparator();
        presets_.addItem (names[i], i + 1);
    }
    shownPreset_.clear();
}

void TopBar::refresh()
{
    auto& pm = processor_.getPresetManager();
    const auto name = pm.getCurrentName();
    if (name != shownPreset_)
    {
        if (pm.getCurrentIndex() < 0 || presets_.getNumItems() != pm.getPresetNames().size())
            rebuildPresetList();
        presets_.setSelectedItemIndex (pm.getCurrentIndex(), juce::dontSendNotification);
        if (pm.getCurrentIndex() < 0)
            presets_.setText (name, juce::dontSendNotification);
        shownPreset_ = name;
        presets_.setTooltip ("Preset: " + name);
    }

    slotA_.setToggleState (processor_.getActiveSlot() == 0, juce::dontSendNotification);
    slotB_.setToggleState (processor_.getActiveSlot() == 1, juce::dontSendNotification);
    copy_.setButtonText (processor_.getActiveSlot() == 0 ? "A>B" : "B>A");

    const double sr = processor_.getEngine().getSampleRate();
    const int latency = processor_.getLatencySamples();
    const auto text = juce::String (sr / 1000.0, 1) + " kHz  " + juce::String (1000.0 * latency / sr, 1) + " ms";
    if (text != infoText_)
    {
        infoText_ = text;
        repaint (infoArea_);
    }
}

void TopBar::showSaveDialog()
{
    saveDialog_ = std::make_unique<juce::AlertWindow> ("Save preset", "Name for this user preset:", juce::MessageBoxIconType::NoIcon, this);
    saveDialog_->addTextEditor ("name", processor_.getPresetManager().getCurrentName());
    saveDialog_->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog_->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog_->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result) {
        if (result == 1 && saveDialog_ != nullptr)
        {
            if (processor_.getPresetManager().saveUserPreset (saveDialog_->getTextEditorContents ("name")))
                rebuildPresetList();
            refresh();
        }
        saveDialog_.reset();
    }));
}

juce::Path TopBar::lotusPath (juce::Rectangle<float> b)
{
    // Five petals fanning up from a common base: an original lotus mark.
    juce::Path p;
    const auto base = juce::Point<float> (b.getCentreX(), b.getBottom());
    const float h = b.getHeight(), w = b.getWidth();
    struct Petal { float angle, length, width; };
    for (const auto& petal : { Petal { 0.0f, 1.0f, 0.30f }, Petal { -0.55f, 0.82f, 0.26f }, Petal { 0.55f, 0.82f, 0.26f },
                               Petal { -1.1f, 0.6f, 0.22f }, Petal { 1.1f, 0.6f, 0.22f } })
    {
        juce::Path leaf;
        const float len = h * petal.length, wid = w * petal.width;
        leaf.startNewSubPath (0.0f, 0.0f);
        leaf.cubicTo (-wid, -len * 0.35f, -wid * 0.55f, -len * 0.85f, 0.0f, -len);
        leaf.cubicTo (wid * 0.55f, -len * 0.85f, wid, -len * 0.35f, 0.0f, 0.0f);
        leaf.closeSubPath();
        p.addPath (leaf, juce::AffineTransform::rotation (petal.angle).translated (base));
    }
    return p;
}

void TopBar::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat(), 9.0f, Colours::panel);

    auto logo = logoArea_.toFloat();
    const auto mark = logo.removeFromLeft (26.0f).withSizeKeepingCentre (24.0f, 20.0f);
    g.setColour (Colours::accent.withAlpha (0.85f));
    g.fillPath (lotusPath (mark));
    g.setColour (Colours::background);
    g.strokePath (lotusPath (mark), juce::PathStrokeType (1.0f));

    logo.removeFromLeft (8.0f);
    g.setColour (Colours::text);
    g.setFont (font (21.0f, Weight::bold).withExtraKerningFactor (0.12f));
    g.drawText ("EVOX", logo.removeFromTop (logo.getHeight() * 0.68f), juce::Justification::bottomLeft);
    g.setColour (Colours::textDim);
    g.setFont (font (8.5f, Weight::semiBold).withExtraKerningFactor (0.2f));
    g.drawText ("LOTUS SOUNDS", logo, juce::Justification::topLeft);

    g.setColour (Colours::textDim);
    g.setFont (font (11.5f, Weight::medium));
    g.drawText (infoText_, infoArea_, juce::Justification::centredRight);
}

void TopBar::resized()
{
    auto b = getLocalBounds().reduced (10, 9);

    power_.setBounds (b.removeFromLeft (28).withSizeKeepingCentre (28, 28));
    b.removeFromLeft (10);
    logoArea_ = b.removeFromLeft (130);

    // Right cluster: Input Type, readout, A/B.
    // Keep the right cluster wide enough for "Alto/Tenor" + readout; the preset browser
    // in the middle is what gives way on narrow windows.
    auto right = b.removeFromRight (juce::jlimit (330, 380, b.getWidth() / 2));
    copy_.setBounds (right.removeFromRight (40));
    right.removeFromRight (4);
    slotB_.setBounds (right.removeFromRight (28));
    right.removeFromRight (2);
    slotA_.setBounds (right.removeFromRight (28));
    right.removeFromRight (10);
    infoArea_ = right.removeFromRight (juce::jmin (100, right.getWidth() - 110));
    right.removeFromRight (8);
    inputType_.setBounds (right.removeFromRight (juce::jmin (124, right.getWidth())));

    // Centre: preset browser.
    auto centre = b.reduced (12, 0);
    const int width = juce::jmin (330, centre.getWidth());
    centre = centre.withSizeKeepingCentre (width, centre.getHeight());
    prev_.setBounds (centre.removeFromLeft (28));
    save_.setBounds (centre.removeFromRight (52));
    centre.removeFromRight (4);
    next_.setBounds (centre.removeFromRight (28));
    presets_.setBounds (centre.reduced (4, 0));
}

// ------------------------------------------------------------------------------------

VibratoPanel::VibratoPanel (juce::AudioProcessorValueTreeState& s)
    : rate_ (s, ParamID::vibratoRate, "Rate", Knob::Size::small, "Vibrato rate"),
      depth_ (s, ParamID::vibratoDepth, "Depth", Knob::Size::small, "Vibrato depth (peak, cents)"),
      delay_ (s, ParamID::vibratoDelay, "Delay", Knob::Size::small, "Onset delay after each new note")
{
    on_.setTooltip ("Create Vibrato on/off");
    onAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::vibratoOn, on_);
    shape_.addItemList ({ "Sine", "Square", "Saw" }, 1);
    shapeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, ParamID::vibratoShape, shape_);

    for (auto* c : std::initializer_list<juce::Component*> { &on_, &shape_, &rate_, &depth_, &delay_ })
        addChildComponent (c);
    on_.setVisible (true);
}

void VibratoPanel::setExpanded (bool shouldBeExpanded)
{
    expanded_ = shouldBeExpanded;
    for (auto* c : std::initializer_list<juce::Component*> { &shape_, &rate_, &depth_, &delay_ })
        c->setVisible (expanded_);
    repaint();
    if (onToggle)
        onToggle();
}

void VibratoPanel::mouseUp (const juce::MouseEvent& e)
{
    if (e.y < headerHeight && e.x < getWidth() - 50)
        setExpanded (! expanded_);
}

void VibratoPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat(), 8.0f, expanded_ ? Colours::panelRaised : Colours::panel);
    auto header = getLocalBounds().removeFromTop (headerHeight).toFloat().reduced (10.0f, 0.0f);

    juce::Path chevron;
    const auto c = header.removeFromLeft (10.0f).getCentre();
    if (expanded_)
    {
        chevron.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
        chevron.lineTo (c.x, c.y + 2.0f);
        chevron.lineTo (c.x + 4.0f, c.y - 2.0f);
    }
    else
    {
        chevron.startNewSubPath (c.x - 2.0f, c.y - 4.0f);
        chevron.lineTo (c.x + 2.0f, c.y);
        chevron.lineTo (c.x - 2.0f, c.y + 4.0f);
    }
    g.setColour (Colours::textDim);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    drawCaption (g, "Create Vibrato", header.withTrimmedLeft (6.0f));
}

void VibratoPanel::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (headerHeight);
    on_.setBounds (header.removeFromRight (46).reduced (4, 4));

    b.reduce (8, 4);
    shape_.setBounds (b.removeFromTop (24));
    b.removeFromTop (6);
    const int w = b.getWidth() / 3;
    rate_.setBounds (b.removeFromLeft (w));
    depth_.setBounds (b.removeFromLeft (w));
    delay_.setBounds (b);
}

} // namespace evox::ui
