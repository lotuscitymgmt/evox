#include "PitchDisplay.h"

#include "../Parameters.h"

namespace evox::ui
{

PitchDisplay::PitchDisplay()
{
    history_.resize (4096);
    targets_.fill (true);
    setOpaque (true);
}

void PitchDisplay::addFrame (const dsp::AnalysisFrame& f, double sampleRate)
{
    Point p;
    p.time = static_cast<double> (f.sample) / sampleRate;
    p.inMidi = f.voiced && f.inputHz > 0.0f ? dsp::hzToMidi (f.inputHz, referenceHz_) : 0.0f;
    p.outMidi = f.voiced && f.outputHz > 0.0f ? dsp::hzToMidi (f.outputHz, referenceHz_) : 0.0f;
    p.targetMidi = f.voiced ? f.targetMidi : -1.0f;

    // A host restart / sample-rate change makes time jump backwards: start over.
    if (count_ > 0 && p.time + 0.5 < latestTime_)
        count_ = 0;

    history_[head_] = p;
    head_ = (head_ + 1) % history_.size();
    count_ = std::min (count_ + 1, history_.size());

    latestTime_ = p.time;
    wallAtLatest_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
    currentTarget_ = p.targetMidi;

    // Follow the singer: ease the vertical centre towards the newest pitch.
    if (p.inMidi > 0.0f)
    {
        const float lo = centreMidi_ - spanSemitones_ * 0.35f, hi = centreMidi_ + spanSemitones_ * 0.35f;
        if (p.inMidi < lo || p.inMidi > hi)
            centreMidi_ += (p.inMidi - centreMidi_) * 0.02f;
    }
}

void PitchDisplay::tick (double nowSeconds)
{
    // Frames arrive in audio-block bursts; interpolate the scroll with the wall clock so it
    // glides at 60 fps, but never run more than 60 ms ahead of real data (stops when the
    // transport stops).
    const double ahead = juce::jlimit (0.0, 0.06, nowSeconds - wallAtLatest_);
    displayTime_ = latestTime_ + ahead;
    lastWallClock_ = nowSeconds;
    repaint();
}

float PitchDisplay::yForMidi (float midi, juce::Rectangle<float> area) const noexcept
{
    const float top = centreMidi_ + spanSemitones_ * 0.5f;
    return area.getY() + (top - midi) / spanSemitones_ * area.getHeight();
}

void PitchDisplay::drawTrace (juce::Graphics& g, juce::Rectangle<float> area, bool output) const
{
    if (count_ < 2)
        return;

    juce::Path path;
    bool penDown = false;
    double prevTime = 0.0;
    const double start = displayTime_ - visibleSeconds;

    for (size_t i = 0; i < count_; ++i)
    {
        const auto& p = history_[(head_ + history_.size() - count_ + i) % history_.size()];
        if (p.time < start - 0.05)
            continue;

        const float midi = output ? p.outMidi : p.inMidi;
        const float x = area.getX() + static_cast<float> ((p.time - start) / visibleSeconds) * area.getWidth();

        if (midi <= 0.0f || (penDown && p.time - prevTime > 0.03))
        {
            penDown = false;
            if (midi <= 0.0f)
                continue;
        }

        const float y = yForMidi (midi, area);
        if (! penDown)
        {
            path.startNewSubPath (x, y);
            penDown = true;
        }
        else
        {
            path.lineTo (x, y);
        }
        prevTime = p.time;
    }

    const auto colour = output ? Colours::accent : Colours::cyan;
    g.setColour (colour.withAlpha (0.18f));
    g.strokePath (path, juce::PathStrokeType (output ? 6.0f : 5.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (output ? 2.2f : 1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

void PitchDisplay::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    auto bounds = getLocalBounds().toFloat();
    drawPanel (g, bounds, 9.0f, Colours::panel);

    auto inner = bounds.reduced (1.0f);
    const auto axis = inner.removeFromLeft (40.0f);
    const auto plot = inner;

    juce::Path clip;
    clip.addRoundedRectangle (bounds.reduced (1.0f), 8.0f);
    g.saveState();
    g.reduceClipRegion (clip);

    // Semitone rows.
    const int lowNote = static_cast<int> (std::floor (centreMidi_ - spanSemitones_ * 0.5f)) - 1;
    const int highNote = static_cast<int> (std::ceil (centreMidi_ + spanSemitones_ * 0.5f)) + 1;
    const float rowHeight = plot.getHeight() / spanSemitones_;

    for (int n = lowNote; n <= highNote; ++n)
    {
        const float y = yForMidi (static_cast<float> (n), plot);
        const auto row = juce::Rectangle<float> (plot.getX(), y - rowHeight * 0.5f, plot.getWidth(), rowHeight);
        const int pc = ((n % 12) + 12) % 12;

        if (! targets_[static_cast<size_t> (pc)])
        {
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRect (row);
        }

        g.setColour (pc == 0 ? Colours::lineStrong : Colours::line.withAlpha (0.7f));
        g.drawHorizontalLine (juce::roundToInt (row.getBottom()), plot.getX(), plot.getRight());

        if (rowHeight >= 9.0f || pc == 0)
        {
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            g.setColour (pc == 0 ? Colours::text : (black ? Colours::textFaint : Colours::textDim));
            g.setFont (font (juce::jlimit (8.5f, 11.0f, rowHeight * 0.8f), pc == 0 ? Weight::semiBold : Weight::regular));
            g.drawText (noteNames()[pc] + (pc == 0 ? juce::String (n / 12 - 1) : juce::String()),
                        juce::Rectangle<float> (axis.getX() + 6.0f, row.getY(), axis.getWidth() - 10.0f, rowHeight),
                        juce::Justification::centredRight);
        }
    }

    // Time grid: one line per second, moving with the scroll.
    g.setColour (Colours::line.withAlpha (0.5f));
    for (double s = std::floor (displayTime_ - visibleSeconds); s <= displayTime_; s += 1.0)
    {
        const float x = plot.getX() + static_cast<float> ((s - (displayTime_ - visibleSeconds)) / visibleSeconds) * plot.getWidth();
        if (x > plot.getX())
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }

    // Current target note band.
    if (currentTarget_ > 0.0f)
    {
        const float y = yForMidi (currentTarget_, plot);
        const auto band = juce::Rectangle<float> (plot.getX(), y - rowHeight * 0.5f, plot.getWidth(), rowHeight);
        g.setGradientFill (juce::ColourGradient (Colours::accent.withAlpha (0.0f), band.getX(), y,
                                                 Colours::accent.withAlpha (0.16f), band.getRight(), y, false));
        g.fillRect (band);
    }

    g.setColour (Colours::line);
    g.drawVerticalLine (juce::roundToInt (plot.getX()), plot.getY(), plot.getBottom());

    g.reduceClipRegion (plot.toNearestInt());
    drawTrace (g, plot, false);
    drawTrace (g, plot, true);
    g.restoreState();

    // Legend
    auto legend = bounds.reduced (12.0f, 8.0f).removeFromTop (14.0f).removeFromRight (150.0f);
    g.setFont (font (10.5f, Weight::semiBold));
    g.setColour (Colours::cyan);
    g.fillRoundedRectangle (legend.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 3.0f), 1.5f);
    g.drawText ("INPUT", legend.removeFromLeft (55.0f).withTrimmedLeft (5.0f), juce::Justification::centredLeft);
    g.setColour (Colours::accent);
    g.fillRoundedRectangle (legend.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 3.0f), 1.5f);
    g.drawText ("OUTPUT", legend.withTrimmedLeft (5.0f), juce::Justification::centredLeft);
}

void PitchDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    spanSemitones_ = juce::jlimit (8.0f, 48.0f, spanSemitones_ * (w.deltaY > 0 ? 0.9f : 1.1f));
    repaint();
}

void PitchDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    spanSemitones_ = 18.0f;
    repaint();
}

// ------------------------------------------------------------------------------------

void CorrectionMeter::setCents (float cents, bool active) noexcept
{
    target_ = juce::jlimit (-100.0f, 100.0f, cents);
    active_ = active;
    // Light smoothing for readability; still tracks a 0 ms step within ~3 frames.
    shown_ += ((active_ ? target_ : 0.0f) - shown_) * 0.5f;
    repaint();
}

void CorrectionMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b, 8.0f, Colours::panel);

    auto inner = b.reduced (12.0f, 0.0f);
    auto label = inner.removeFromLeft (92.0f);
    drawCaption (g, "Correction", label);

    auto value = inner.removeFromRight (64.0f);
    g.setFont (font (12.5f, Weight::semiBold));
    g.setColour (active_ ? Colours::text : Colours::textFaint);
    const int cents = juce::roundToInt (shown_);
    g.drawText ((cents > 0 ? "+" : "") + juce::String (cents) + " ct", value, juce::Justification::centredRight);

    auto bar = inner.reduced (8.0f, 0.0f).withSizeKeepingCentre (inner.getWidth() - 16.0f, 8.0f);
    g.setColour (Colours::background);
    g.fillRoundedRectangle (bar, 4.0f);

    // Ticks at -100, -50, 0, +50, +100 cents.
    for (int c = -100; c <= 100; c += 25)
    {
        const float x = bar.getCentreX() + bar.getWidth() * 0.5f * static_cast<float> (c) / 100.0f;
        g.setColour (c == 0 ? Colours::textDim : Colours::line.brighter (0.2f));
        const float half = c == 0 ? 9.0f : (c % 50 == 0 ? 6.0f : 4.0f);
        g.drawLine (x, bar.getCentreY() - half, x, bar.getCentreY() + half, 1.0f);
    }

    const float x = bar.getCentreX() + bar.getWidth() * 0.5f * shown_ / 100.0f;
    auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x, bar.getCentreX()), bar.getY(),
                                                            juce::jmax (x, bar.getCentreX()), bar.getBottom());
    g.setColour (Colours::accent.withAlpha (active_ ? 1.0f : 0.3f));
    g.fillRoundedRectangle (fill, 3.0f);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x, bar.getCentreY() }));
}

} // namespace evox::ui
