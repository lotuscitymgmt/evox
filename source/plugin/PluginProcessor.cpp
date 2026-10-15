#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace evox
{

namespace
{
constexpr auto stateTag = "EVOXState";
constexpr int stateVersion = 1;
} // namespace

EvoxAudioProcessor::EvoxAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, stateTag, createParameterLayout())
{
    bypass_ = apvts_.getRawParameterValue (ParamID::bypass);
    inputType_ = apvts_.getRawParameterValue (ParamID::inputType);
    tracking_ = apvts_.getRawParameterValue (ParamID::tracking);
    lowCut_ = apvts_.getRawParameterValue (ParamID::lowCut);
    mix_ = apvts_.getRawParameterValue (ParamID::mix);
    outputGain_ = apvts_.getRawParameterValue (ParamID::outputGain);

    // Report a sensible latency before prepareToPlay so scanners see the real value.
    setLatencySamples (dsp::EvoxEngine::latencyFor (44100.0));
}

bool EvoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void EvoxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    engine_.setParams (readParams());
    setLatencySamples (engine_.getLatencySamples());
}

void EvoxAudioProcessor::reset()
{
    engine_.reset();
}

dsp::EngineParams EvoxAudioProcessor::readParams() const noexcept
{
    dsp::EngineParams p;
    p.bypass = bypass_->load() >= 0.5f;
    p.inputType = static_cast<dsp::InputType> (juce::jlimit (0, 3, static_cast<int> (inputType_->load())));
    p.tracking = tracking_->load() * 0.01f;
    p.lowCut = lowCut_->load() >= 0.5f;
    p.mix = mix_->load() * 0.01f;
    p.outputGainDb = outputGain_->load();
    return p;
}

void EvoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine_.setParams (readParams());
    engine_.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void EvoxAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host-side bypass: keep the plugin's reported latency so the track stays aligned.
    engine_.processBypassed (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

juce::AudioProcessorParameter* EvoxAudioProcessor::getBypassParameter() const
{
    return apvts_.getParameter (ParamID::bypass);
}

juce::AudioProcessorEditor* EvoxAudioProcessor::createEditor()
{
    return new EvoxEditor (*this);
}

void EvoxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    state.setProperty ("stateVersion", stateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void EvoxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts_.state.getType()))
            apvts_.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace evox

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new evox::EvoxAudioProcessor();
}
