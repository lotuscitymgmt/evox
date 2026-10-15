#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace evox
{

/** Phase 1-2 developer editor: a live pitch readout above JUCE's generic parameter
    panel. Replaced by the full EVOX UI in Phase 5. Reads analysis data only via the
    lock-free FIFO on a 30 Hz timer; never touches the audio thread. */
class EvoxEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EvoxEditor (EvoxAudioProcessor&);
    ~EvoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    EvoxAudioProcessor& processor_;
    juce::GenericAudioProcessorEditor generic_;
    dsp::AnalysisFrame latest_;
    bool haveFrame_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EvoxEditor)
};

} // namespace evox
