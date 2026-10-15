#include "NoteKeyboard.h"

#include "../Parameters.h"

namespace evox::ui
{

namespace
{
bool isBlack (int pc) noexcept { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

bool inScale (int pc, int key, int scale) noexcept
{
    static constexpr int major[] { 0, 2, 4, 5, 7, 9, 11 }, minor[] { 0, 2, 3, 5, 7, 8, 10 };
    if (scale == 0)
        return true;
    for (int step : (scale == 1 ? major : minor))
        if ((key + step) % 12 == pc)
            return true;
    return false;
}
} // namespace

NoteKeyboard::NoteKeyboard (juce::AudioProcessorValueTreeState& s) : state_ (s)
{
    setTooltip ("Click: enable / remove / bypass this note.\nRight-click: follow Key & Scale again.");
    refresh();
}

void NoteKeyboard::refresh()
{
    bool changed = false;
    auto read = [&] (const juce::String& id, int& dst) {
        const int v = juce::roundToInt (state_.getRawParameterValue (id)->load());
        changed |= v != dst;
        dst = v;
    };
    read (ParamID::key, key_);
    read (ParamID::scale, scale_);
    for (int pc = 0; pc < 12; ++pc)
        read (ParamID::note (pc), modes_[static_cast<size_t> (pc)]);
    if (changed)
        repaint();
}

void NoteKeyboard::setCurrentTarget (int pc) noexcept
{
    if (pc != target_)
    {
        target_ = pc;
        repaint();
    }
}

NoteKeyboard::Effective NoteKeyboard::effectiveState (int pc) const noexcept
{
    switch (static_cast<NoteState> (modes_[static_cast<size_t> (pc)]))
    {
        case NoteState::enabled:  return Effective::enabled;
        case NoteState::removed:  return Effective::removed;
        case NoteState::bypassed: return Effective::bypassed;
        case NoteState::scale:    break;
    }
    return inScale (pc, key_, scale_) ? Effective::enabled : Effective::removed;
}

std::array<bool, 12> NoteKeyboard::getTargets() const noexcept
{
    std::array<bool, 12> t {};
    for (int pc = 0; pc < 12; ++pc)
        t[static_cast<size_t> (pc)] = effectiveState (pc) == Effective::enabled;
    return t;
}

juce::Rectangle<float> NoteKeyboard::keyBounds (int pc) const noexcept
{
    const auto b = getLocalBounds().toFloat();
    const float rowH = b.getHeight() / 12.0f;
    auto row = juce::Rectangle<float> (b.getX(), b.getBottom() - rowH * float (pc + 1), b.getWidth(), rowH).reduced (0.0f, 1.0f);
    if (isBlack (pc))
        row = row.withTrimmedRight (b.getWidth() * 0.32f);
    return row;
}

int NoteKeyboard::pitchClassAt (juce::Point<float> p) const noexcept
{
    for (int pc = 0; pc < 12; ++pc)
        if (keyBounds (pc).contains (p))
            return pc;
    return -1;
}

void NoteKeyboard::setNoteMode (int pc, int mode)
{
    if (auto* p = state_.getParameter (ParamID::note (pc)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (mode)));
        p->endChangeGesture();
    }
    refresh();
}

void NoteKeyboard::mouseDown (const juce::MouseEvent& e)
{
    const int pc = pitchClassAt (e.position);
    if (pc < 0)
        return;

    if (e.mods.isPopupMenu() || e.mods.isAltDown())
    {
        setNoteMode (pc, static_cast<int> (NoteState::scale));
        return;
    }

    // Cycle the *effective* state so the first click always visibly changes the key.
    switch (effectiveState (pc))
    {
        case Effective::enabled:  setNoteMode (pc, static_cast<int> (NoteState::removed)); break;
        case Effective::removed:  setNoteMode (pc, static_cast<int> (NoteState::bypassed)); break;
        case Effective::bypassed: setNoteMode (pc, static_cast<int> (NoteState::enabled)); break;
    }
}

void NoteKeyboard::mouseMove (const juce::MouseEvent& e)
{
    const int pc = pitchClassAt (e.position);
    if (pc != hover_)
    {
        hover_ = pc;
        repaint();
    }
}

void NoteKeyboard::mouseExit (const juce::MouseEvent&)
{
    hover_ = -1;
    repaint();
}

void NoteKeyboard::paint (juce::Graphics& g)
{
    // White keys first, then black keys on top (piano layout turned on its side).
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int pc = 0; pc < 12; ++pc)
        {
            if (isBlack (pc) != (pass == 1))
                continue;

            const auto r = keyBounds (pc);
            const auto st = effectiveState (pc);
            const bool black = isBlack (pc);
            const bool overridden = modes_[static_cast<size_t> (pc)] != static_cast<int> (NoteState::scale);

            juce::Colour fill = black ? juce::Colour (0xff22252c) : juce::Colour (0xff2c3038);
            juce::Colour textColour = Colours::text;
            if (st == Effective::enabled)
                fill = black ? Colours::accent.withAlpha (0.55f) : Colours::accent.withAlpha (0.75f);
            else if (st == Effective::bypassed)
                fill = black ? juce::Colour (0xff1b2a2d) : juce::Colour (0xff1f3236);
            else
                textColour = Colours::textFaint;

            if (pc == hover_)
                fill = fill.brighter (0.15f);

            g.setColour (fill);
            g.fillRoundedRectangle (r, 4.0f);

            if (st == Effective::bypassed)
            {
                g.setColour (Colours::cyan);
                g.drawRoundedRectangle (r.reduced (0.75f), 4.0f, 1.5f);
                textColour = Colours::cyan;
            }
            else if (st == Effective::removed)
            {
                g.setColour (Colours::textFaint.withAlpha (0.6f));
                g.drawLine (r.getX() + 8.0f, r.getCentreY(), r.getRight() - 8.0f, r.getCentreY(), 1.0f);
            }

            if (pc == target_ && st == Effective::enabled)
            {
                g.setColour (Colours::text);
                g.drawRoundedRectangle (r.reduced (0.75f), 4.0f, 2.0f);
            }

            const float textH = juce::jlimit (9.0f, 12.5f, r.getHeight() * 0.62f);
            g.setFont (font (textH, Weight::semiBold));
            g.setColour (textColour);
            g.drawText (noteNames()[pc], r.reduced (8.0f, 0.0f), juce::Justification::centredLeft);

            if (st == Effective::bypassed)
            {
                g.setFont (font (textH * 0.78f, Weight::bold));
                g.drawText ("BYP", r.reduced (8.0f, 0.0f), juce::Justification::centredRight);
            }

            if (overridden)
            {
                g.setColour (st == Effective::bypassed ? Colours::cyan : Colours::text);
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ r.getRight() - (st == Effective::bypassed ? 34.0f : 10.0f), r.getCentreY() }));
            }
        }
    }
}

// ------------------------------------------------------------------------------------

LevelMeter::LevelMeter (juce::String caption) : caption_ (std::move (caption)) {}

void LevelMeter::push (float peak, float rms) noexcept
{
    const float pDb = juce::Decibels::gainToDecibels (peak, -100.0f);
    const float rDb = juce::Decibels::gainToDecibels (rms, -100.0f);

    // Ballistics at 60 Hz: instant attack, ~20 dB/s peak fall, smoother RMS.
    peakDb_ = pDb > peakDb_ ? pDb : juce::jmax (pDb, peakDb_ - 0.35f);
    rmsDb_ = rDb > rmsDb_ ? rmsDb_ + (rDb - rmsDb_) * 0.5f : rmsDb_ + (rDb - rmsDb_) * 0.12f;

    if (pDb >= holdDb_)
    {
        holdDb_ = pDb;
        holdFrames_ = 90; // 1.5 s
    }
    else if (--holdFrames_ <= 0)
    {
        holdDb_ = juce::jmax (peakDb_, holdDb_ - 0.6f);
    }
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawCaption (g, caption_, b.removeFromTop (14.0f), juce::Justification::centred);

    auto readout = b.removeFromBottom (16.0f);
    g.setFont (font (10.5f, Weight::medium));
    g.setColour (holdDb_ > -0.1f ? Colours::accent : Colours::textDim);
    g.drawText (holdDb_ <= -99.0f ? juce::String ("-inf") : juce::String (holdDb_, 1), readout, juce::Justification::centred);

    auto bar = b.reduced (b.getWidth() * 0.28f, 2.0f);
    g.setColour (Colours::background);
    g.fillRoundedRectangle (bar, 3.0f);

    auto level = [&] (float db) { return bar.getBottom() - toPos (db) * bar.getHeight(); };

    // RMS body (solid) with a pink zone above -6 dBFS, peak as a translucent extension.
    const float yPeak = level (peakDb_), yRms = level (rmsDb_), y6 = level (-6.0f);
    g.setColour (Colours::cyan.withAlpha (0.25f));
    g.fillRect (bar.withTop (yPeak));
    g.setColour (Colours::cyan.withAlpha (0.85f));
    g.fillRect (bar.withTop (yRms));
    if (yRms < y6)
    {
        g.setColour (Colours::accent);
        g.fillRect (bar.withTop (yRms).withBottom (y6));
    }

    g.setColour (holdDb_ > -0.1f ? Colours::accent : Colours::text);
    g.fillRect (bar.withTop (level (holdDb_)).withHeight (1.5f));

    // 0 dB and -12 / -24 / -48 ticks
    for (float db : { 0.0f, -12.0f, -24.0f, -48.0f })
    {
        g.setColour (db > -1.0f ? Colours::textDim : Colours::line.brighter (0.2f));
        g.drawHorizontalLine (juce::roundToInt (level (db)), bar.getRight() + 2.0f, bar.getRight() + 5.0f);
    }
}

} // namespace evox::ui
