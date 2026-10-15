#pragma once

#include "Parameters.h"
#include "dsp/EvoxEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace evox
{

class EvoxAudioProcessor final : public juce::AudioProcessor
{
public:
    EvoxAudioProcessor();
    ~EvoxAudioProcessor() override = default;

    // --- AudioProcessor ------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- EVOX ----------------------------------------------------------------
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts_; }
    dsp::EvoxEngine& getEngine() noexcept { return engine_; }

    /** Consumer side of the analysis FIFO (UI thread only). */
    bool popAnalysisFrame (dsp::AnalysisFrame& frame) noexcept { return engine_.getAnalysisFifo().pop (frame); }

private:
    dsp::EngineParams readParams() const noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    dsp::EvoxEngine engine_;

    // Cached raw parameter pointers: lock-free reads on the audio thread.
    std::atomic<float>* bypass_ = nullptr;
    std::atomic<float>* inputType_ = nullptr;
    std::atomic<float>* tracking_ = nullptr;
    std::atomic<float>* lowCut_ = nullptr;
    std::atomic<float>* mix_ = nullptr;
    std::atomic<float>* outputGain_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EvoxAudioProcessor)
};

} // namespace evox
