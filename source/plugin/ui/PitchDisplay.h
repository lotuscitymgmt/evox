#pragma once

#include "Theme.h"
#include "dsp/EvoxEngine.h"

#include <vector>

namespace evox::ui
{

/** Scrolling piano-roll pitch display (the hero element).
    - cyan line: detected input pitch; pink line: corrected output pitch
    - faint band on the current target note; rows of notes that are not targets are shaded
    - the output line is drawn exactly as measured (no smoothing), so at 0 ms retune it
      visibly steps between notes
    Fed from the editor's 60 Hz timer with frames popped from the lock-free FIFO; owns its
    own history so drawing never touches the audio thread. Mouse wheel zooms vertically. */
class PitchDisplay final : public juce::Component
{
public:
    PitchDisplay();

    void addFrame (const dsp::AnalysisFrame&, double sampleRate);
    /** Advance the scroll position; called every UI tick. */
    void tick (double nowSeconds);
    void setReferencePitch (float hz) noexcept { referenceHz_ = hz; }
    void setNoteStates (const std::array<bool, 12>& isTarget) noexcept
    {
        dirty_ |= isTarget != targets_;
        targets_ = isTarget;
    }

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr double visibleSeconds = 4.0;

private:
    struct Point
    {
        double time = 0.0;     // seconds of input audio
        float inMidi = 0.0f;   // 0 = none
        float outMidi = 0.0f;
        float targetMidi = -1.0f;
    };

    float yForMidi (float midi, juce::Rectangle<float> area) const noexcept;
    void drawTrace (juce::Graphics&, juce::Rectangle<float> area, bool output) const;

    std::vector<Point> history_;
    size_t head_ = 0, count_ = 0;

    double latestTime_ = 0.0, displayTime_ = 0.0, lastWallClock_ = 0.0, wallAtLatest_ = 0.0;
    float referenceHz_ = 440.0f;
    float centreMidi_ = 60.0f, spanSemitones_ = 18.0f;
    float currentTarget_ = -1.0f;
    std::array<bool, 12> targets_ {};
    bool dirty_ = true;
};

/** Center-zero horizontal meter: cents of correction applied right now (+/-100). */
class CorrectionMeter final : public juce::Component
{
public:
    CorrectionMeter() { setOpaque (true); }
    void setCents (float cents, bool active) noexcept;
    void paint (juce::Graphics&) override;

private:
    float shown_ = 0.0f, target_ = 0.0f;
    bool active_ = false, everPainted_ = false;
};

} // namespace evox::ui
