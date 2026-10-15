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
    auto get = [this] (const juce::String& id) {
        auto* p = apvts_.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    raw_.bypass = get (ParamID::bypass);
    raw_.inputType = get (ParamID::inputType);
    raw_.tracking = get (ParamID::tracking);
    raw_.lowCut = get (ParamID::lowCut);
    raw_.key = get (ParamID::key);
    raw_.scale = get (ParamID::scale);
    for (int pc = 0; pc < 12; ++pc)
        raw_.notes[static_cast<size_t> (pc)] = get (ParamID::note (pc));
    raw_.retuneSpeed = get (ParamID::retuneSpeed);
    raw_.humanize = get (ParamID::humanize);
    raw_.naturalVibrato = get (ParamID::naturalVibrato);
    raw_.transpose = get (ParamID::transpose);
    raw_.detune = get (ParamID::detune);
    raw_.referencePitch = get (ParamID::referencePitch);
    raw_.vibratoOn = get (ParamID::vibratoOn);
    raw_.vibratoShape = get (ParamID::vibratoShape);
    raw_.vibratoRate = get (ParamID::vibratoRate);
    raw_.vibratoDepth = get (ParamID::vibratoDepth);
    raw_.vibratoDelay = get (ParamID::vibratoDelay);
    raw_.character = get (ParamID::character);
    raw_.formantOn = get (ParamID::formantOn);
    raw_.throat = get (ParamID::throat);
    raw_.mix = get (ParamID::mix);
    raw_.outputGain = get (ParamID::outputGain);

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
    auto flag = [] (const std::atomic<float>* v) { return v->load() >= 0.5f; };
    auto index = [] (const std::atomic<float>* v, int maxIndex) {
        return juce::jlimit (0, maxIndex, static_cast<int> (std::lround (v->load())));
    };

    dsp::EngineParams p;
    p.bypass = flag (raw_.bypass);
    p.inputType = static_cast<dsp::InputType> (index (raw_.inputType, 3));
    p.tracking = raw_.tracking->load() * 0.01f;
    p.lowCut = flag (raw_.lowCut);
    p.mix = raw_.mix->load() * 0.01f;
    p.outputGainDb = raw_.outputGain->load();

    auto& c = p.correction;
    c.key = index (raw_.key, 11);
    c.scale = static_cast<dsp::ScaleType> (index (raw_.scale, 2));
    for (size_t pc = 0; pc < 12; ++pc)
        c.noteModes[pc] = static_cast<dsp::NoteMode> (index (raw_.notes[pc], 3));
    c.retuneMs = raw_.retuneSpeed->load();
    c.humanize = raw_.humanize->load() * 0.01f;
    c.naturalVibratoDb = raw_.naturalVibrato->load();
    c.transpose = raw_.transpose->load();
    c.detuneCents = raw_.detune->load();
    c.referenceHz = raw_.referencePitch->load();
    c.vibratoOn = flag (raw_.vibratoOn);
    c.vibratoShape = static_cast<dsp::VibratoShape> (index (raw_.vibratoShape, 2));
    c.vibratoRateHz = raw_.vibratoRate->load();
    c.vibratoDepthCents = raw_.vibratoDepth->load();
    c.vibratoDelayMs = raw_.vibratoDelay->load();

    p.shifter.character = raw_.character->load() * 0.01f;
    p.shifter.formantCorrection = flag (raw_.formantOn);
    p.shifter.throat = raw_.throat->load() * 0.01f;
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

void EvoxAudioProcessor::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot_)
        return;

    abSlots_[static_cast<size_t> (activeSlot_)] = apvts_.copyState();
    auto& target = abSlots_[static_cast<size_t> (slot)];
    if (target.isValid())
        apvts_.replaceState (target.createCopy());
    activeSlot_ = slot;
}

void EvoxAudioProcessor::copyActiveSlotToOther()
{
    abSlots_[static_cast<size_t> (1 - activeSlot_)] = apvts_.copyState();
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
