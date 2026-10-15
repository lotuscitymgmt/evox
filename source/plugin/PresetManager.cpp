#include "PresetManager.h"
#include "Parameters.h"

namespace evox
{

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : state_ (s)
{
    using namespace ParamID;

    // Era-targeted factory set. Values are plain parameter units.
    factory_ = {
        { "Hard Tune 0ms",
          { { retuneSpeed, 0.0f }, { humanize, 0.0f }, { character, 85.0f }, { tracking, 40.0f },
            { naturalVibrato, 0.0f } } },
        { "Natural Correction",
          { { retuneSpeed, 80.0f }, { humanize, 60.0f }, { character, 15.0f }, { tracking, 55.0f },
            { naturalVibrato, 0.0f } } },
        { "Low Male Smooth",
          { { inputType, 2.0f }, { retuneSpeed, 45.0f }, { humanize, 35.0f }, { character, 25.0f },
            { tracking, 60.0f }, { throat, 104.0f } } },
        { "Octave Up Throat Shift",
          { { transpose, 12.0f }, { retuneSpeed, 0.0f }, { character, 60.0f }, { formantOn, 1.0f },
            { throat, 125.0f } } },
        { "Robotic Square Vibrato",
          { { retuneSpeed, 0.0f }, { character, 100.0f }, { naturalVibrato, -12.0f }, { vibratoOn, 1.0f },
            { vibratoShape, 1.0f }, { vibratoRate, 6.0f }, { vibratoDepth, 70.0f }, { vibratoDelay, 0.0f } } },
    };
}

int PresetManager::getNumFactoryPresets() const noexcept
{
    return static_cast<int> (factory_.size());
}

juce::File PresetManager::getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Lotus Sounds").getChildFile ("EVOX").getChildFile ("Presets");
}

juce::Array<juce::File> PresetManager::getUserPresetFiles() const
{
    auto files = getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    files.sort();
    return files;
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : factory_)
        names.add (f.name);
    for (const auto& file : getUserPresetFiles())
        names.add (file.getFileNameWithoutExtension());
    return names;
}

juce::String PresetManager::getCurrentName() const
{
    return state_.state.getProperty (presetProperty, factory_.front().name).toString();
}

int PresetManager::getCurrentIndex() const
{
    return getPresetNames().indexOf (getCurrentName());
}

void PresetManager::setCurrentName (const juce::String& name)
{
    state_.state.setProperty (presetProperty, name, nullptr);
}

void PresetManager::applyFactory (const FactoryPreset& preset)
{
    auto set = [] (juce::RangedAudioParameter* p, float normalised) {
        p->beginChangeGesture();
        p->setValueNotifyingHost (normalised);
        p->endChangeGesture();
    };

    for (auto* p : state_.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->getParameterID() != ParamID::bypass)
                set (ranged, ranged->getDefaultValue());

    for (const auto& [id, value] : preset.values)
        if (auto* p = state_.getParameter (id))
            set (p, p->convertTo0to1 (value));

    setCurrentName (preset.name);
}

void PresetManager::loadPreset (int index)
{
    if (index < 0)
        return;

    if (index < getNumFactoryPresets())
    {
        applyFactory (factory_[static_cast<size_t> (index)]);
        return;
    }

    const auto files = getUserPresetFiles();
    const int userIndex = index - getNumFactoryPresets();
    if (userIndex >= files.size())
        return;

    if (auto xml = juce::XmlDocument::parse (files[userIndex]))
        if (xml->hasTagName (state_.state.getType()))
        {
            state_.replaceState (juce::ValueTree::fromXml (*xml));
            setCurrentName (files[userIndex].getFileNameWithoutExtension());
        }
}

void PresetManager::loadNext()
{
    const auto count = getPresetNames().size();
    if (count > 0)
        loadPreset ((getCurrentIndex() + 1 + count) % count);
}

void PresetManager::loadPrevious()
{
    const auto count = getPresetNames().size();
    if (count > 0)
        loadPreset ((getCurrentIndex() - 1 + count) % count);
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    for (const auto& f : factory_)
        if (f.name.equalsIgnoreCase (name))
            return false; // never shadow a factory preset

    const auto dir = getUserPresetDirectory();
    if (! dir.createDirectory())
        return false;

    setCurrentName (name);
    auto state = state_.copyState();
    if (auto xml = state.createXml())
        return xml->writeTo (dir.getChildFile (name + fileExtension));
    return false;
}

} // namespace evox
