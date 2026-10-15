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
    struct Raw
    {
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* inputType = nullptr;
        std::atomic<float>* tracking = nullptr;
        std::atomic<float>* lowCut = nullptr;
        std::atomic<float>* key = nullptr;
        std::atomic<float>* scale = nullptr;
        std::array<std::atomic<float>*, 12> notes {};
        std::atomic<float>* retuneSpeed = nullptr;
        std::atomic<float>* humanize = nullptr;
        std::atomic<float>* naturalVibrato = nullptr;
        std::atomic<float>* transpose = nullptr;
        std::atomic<float>* detune = nullptr;
        std::atomic<float>* referencePitch = nullptr;
        std::atomic<float>* vibratoOn = nullptr;
        std::atomic<float>* vibratoShape = nullptr;
        std::atomic<float>* vibratoRate = nullptr;
        std::atomic<float>* vibratoDepth = nullptr;
        std::atomic<float>* vibratoDelay = nullptr;
        std::atomic<float>* character = nullptr;
        std::atomic<float>* formantOn = nullptr;
        std::atomic<float>* throat = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* outputGain = nullptr;
    } raw_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EvoxAudioProcessor)
};

} // namespace evox
