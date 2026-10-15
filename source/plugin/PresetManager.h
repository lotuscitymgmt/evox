#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace evox
{

/** Factory + user presets. Message thread only (does file I/O for user presets).
    Factory presets are defined in code as "defaults + overrides", so adding a parameter
    later never breaks them. User presets are APVTS state XML files in
    <user app data>/Lotus Sounds/EVOX/Presets/ as .evoxpreset files. */
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    juce::StringArray getPresetNames() const;
    int getNumFactoryPresets() const noexcept;
    int getCurrentIndex() const;
    juce::String getCurrentName() const;

    void loadPreset (int index);
    void loadNext();
    void loadPrevious();

    /** Saves the current state as a user preset (overwrites a user preset of the same name). */
    bool saveUserPreset (const juce::String& name);

    static juce::File getUserPresetDirectory();
    static constexpr const char* fileExtension = ".evoxpreset";
    static constexpr const char* presetProperty = "presetName";

private:
    struct FactoryPreset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values; // plain (un-normalised) values
    };

    void applyFactory (const FactoryPreset&);
    juce::Array<juce::File> getUserPresetFiles() const;
    void setCurrentName (const juce::String&);

    juce::AudioProcessorValueTreeState& state_;
    std::vector<FactoryPreset> factory_;
};

} // namespace evox
