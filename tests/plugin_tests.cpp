// Plugin-layer tests (JUCE): state, presets, programs, A/B, latency, bypass, layouts.
// Built only with the plugin (needs JUCE). Run: build/evox_plugin_tests_artefacts/Release/evox_plugin_tests

#include "PluginProcessor.h"
#include "common/SignalGen.h"

#include <algorithm>
#include <cstdio>

namespace
{
int failures = 0;

#define EXPECT(cond, msg)                                                                         \
    do                                                                                             \
    {                                                                                              \
        if (! (cond))                                                                              \
        {                                                                                          \
            ++failures;                                                                            \
            std::printf ("  FAILED %s:%d: %s -- %s\n", __FILE__, __LINE__, #cond, juce::String (msg).toRawUTF8()); \
        }                                                                                          \
    } while (false)

float plain (evox::EvoxAudioProcessor& p, const juce::String& id)
{
    auto* param = p.getState().getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void setPlain (evox::EvoxAudioProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getState().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** Runs `seconds` of a sung vowel through processBlock; returns peak |output| (or -1 on NaN/inf). */
float render (evox::EvoxAudioProcessor& p, double fs, double seconds, int block = 256)
{
    const auto voice = evox::tools::synthVoice (fs, seconds, evox::tools::withVibrato (evox::tools::constantPitch (196.0), 5.5, 40.0));
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (size_t pos = 0; pos + static_cast<size_t> (block) <= voice.samples.size(); pos += static_cast<size_t> (block))
    {
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, voice.samples.data() + pos, block, 0.5f);
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = buffer.getSample (ch, i);
                if (! std::isfinite (v))
                    return -1.0f;
                peak = std::max (peak, std::abs (v));
            }
    }
    return peak;
}

void prepare (evox::EvoxAudioProcessor& p, double fs, int block = 256)
{
    p.setPlayConfigDetails (2, 2, fs, block);
    p.prepareToPlay (fs, block);
}

void run (const char* name, const std::function<void()>& fn)
{
    const int before = failures;
    fn();
    std::printf ("[ %s ] %s\n", failures == before ? " OK " : "FAIL", name);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    using namespace evox;

    run ("latency reported to the host matches the engine (< 10 ms)", [] {
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            EvoxAudioProcessor p;
            prepare (p, fs);
            EXPECT (p.getLatencySamples() == dsp::EvoxEngine::latencyFor (fs), fs);
            EXPECT (1000.0 * p.getLatencySamples() / fs < 10.0, fs);
        }
    });

    run ("state save / restore round trip", [] {
        EvoxAudioProcessor a;
        setPlain (a, ParamID::retuneSpeed, 37.5f);
        setPlain (a, ParamID::key, 9.0f);
        setPlain (a, ParamID::scale, 2.0f);
        setPlain (a, ParamID::note (4), 3.0f);
        setPlain (a, ParamID::throat, 140.0f);
        setPlain (a, ParamID::transpose, -7.0f);
        setPlain (a, ParamID::oversampling, 1.0f);
        juce::MemoryBlock blob;
        a.getStateInformation (blob);

        EvoxAudioProcessor b;
        b.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        for (auto* param : a.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
            const auto id = ranged->getParameterID();
            EXPECT (std::abs (plain (a, id) - plain (b, id)) < 1.0e-3f, id);
        }
        EXPECT (std::abs (plain (b, ParamID::retuneSpeed) - 37.5f) < 0.06f, plain (b, ParamID::retuneSpeed));
    });

    run ("garbage state is ignored safely", [] {
        EvoxAudioProcessor p;
        const char junk[] = "not a plugin state at all";
        p.setStateInformation (junk, sizeof (junk));
        EXPECT (std::abs (plain (p, ParamID::retuneSpeed)) < 1.0e-6f, "default retune kept");
    });

    run ("factory presets: names, host programs, values, clean audio", [] {
        EvoxAudioProcessor p;
        prepare (p, 48000.0);
        const juce::StringArray expected { "Hard Tune 0ms", "Natural Correction", "Low Male Smooth",
                                           "Octave Up Throat Shift", "Robotic Square Vibrato" };
        EXPECT (p.getNumPrograms() == expected.size(), p.getNumPrograms());
        for (int i = 0; i < expected.size(); ++i)
        {
            EXPECT (p.getProgramName (i) == expected[i], p.getProgramName (i));
            p.setCurrentProgram (i);
            EXPECT (p.getCurrentProgram() == i, i);
            EXPECT (p.getPresetManager().getCurrentName() == expected[i], expected[i]);
            const float peak = render (p, 48000.0, 1.0);
            EXPECT (peak > 0.05f && peak < 2.0f, expected[i] + ": peak " + juce::String (peak));
        }

        p.setCurrentProgram (0);
        EXPECT (plain (p, ParamID::retuneSpeed) == 0.0f, "hard tune retune");
        EXPECT (plain (p, ParamID::character) > 80.0f, "hard tune character");
        p.setCurrentProgram (3);
        EXPECT (plain (p, ParamID::transpose) == 12.0f && plain (p, ParamID::formantOn) > 0.5f, "octave up");
        p.setCurrentProgram (4);
        EXPECT (plain (p, ParamID::vibratoOn) > 0.5f && plain (p, ParamID::vibratoShape) == 1.0f, "square vibrato");
        // Presets are complete: loading one resets what an earlier preset changed.
        p.setCurrentProgram (0);
        EXPECT (plain (p, ParamID::vibratoOn) < 0.5f && plain (p, ParamID::transpose) == 0.0f, "reset to defaults");
    });

    run ("user preset save / list / load", [] {
        EvoxAudioProcessor p;
        auto& pm = p.getPresetManager();
        const juce::String name = "evox-test-preset-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30));
        setPlain (p, ParamID::throat, 77.0f);
        EXPECT (pm.saveUserPreset (name), "save");
        EXPECT (! pm.saveUserPreset ("Hard Tune 0ms"), "factory names are protected");
        setPlain (p, ParamID::throat, 150.0f);
        const int index = pm.getPresetNames().indexOf (name);
        EXPECT (index >= pm.getNumFactoryPresets(), index);
        pm.loadPreset (index);
        EXPECT (std::abs (plain (p, ParamID::throat) - 77.0f) < 0.1f, plain (p, ParamID::throat));
        PresetManager::getUserPresetDirectory().getChildFile (name + PresetManager::fileExtension).deleteFile();
    });

    run ("A/B compare keeps two independent states", [] {
        EvoxAudioProcessor p;
        setPlain (p, ParamID::character, 20.0f);
        p.selectSlot (1);                       // B starts as a copy of A
        EXPECT (std::abs (plain (p, ParamID::character) - 20.0f) < 0.1f, "B copied");
        setPlain (p, ParamID::character, 90.0f);
        p.selectSlot (0);
        EXPECT (std::abs (plain (p, ParamID::character) - 20.0f) < 0.1f, plain (p, ParamID::character));
        p.selectSlot (1);
        EXPECT (std::abs (plain (p, ParamID::character) - 90.0f) < 0.1f, plain (p, ParamID::character));
        p.copyActiveSlotToOther();
        p.selectSlot (0);
        EXPECT (std::abs (plain (p, ParamID::character) - 90.0f) < 0.1f, "copied B to A");
    });

    run ("bypass paths are latency-compensated and transparent", [] {
        for (int hostBypass = 0; hostBypass < 2; ++hostBypass)
        {
            EvoxAudioProcessor p;
            prepare (p, 48000.0, 512);
            if (! hostBypass)
                setPlain (p, ParamID::bypass, 1.0f);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            int peakAt = -1;
            for (int b = 0; b < 4; ++b)
            {
                buffer.clear();
                if (b == 0)
                    buffer.setSample (0, 100, 1.0f), buffer.setSample (1, 100, 1.0f);
                if (hostBypass)
                    p.processBlockBypassed (buffer, midi);
                else
                    p.processBlock (buffer, midi);
                for (int i = 0; i < 512; ++i)
                    if (std::abs (buffer.getSample (0, i)) > 0.5f)
                        peakAt = b * 512 + i;
            }
            EXPECT (peakAt == 100 + p.getLatencySamples(), juce::String (peakAt) + (hostBypass ? " host" : " param"));
        }
    });

    run ("bus layouts: mono and stereo in = out only", [] {
        EvoxAudioProcessor p;
        auto layout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out) {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (in);
            l.outputBuses.add (out);
            return l;
        };
        EXPECT (p.isBusesLayoutSupported (layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())), "mono");
        EXPECT (p.isBusesLayoutSupported (layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())), "stereo");
        EXPECT (! p.isBusesLayoutSupported (layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())), "mono->stereo");
        EXPECT (! p.isBusesLayoutSupported (layout (juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1())), "5.1");
    });

    run ("oversampling parameter reaches the engine without changing latency", [] {
        EvoxAudioProcessor p;
        prepare (p, 48000.0);
        const int latency = p.getLatencySamples();
        setPlain (p, ParamID::oversampling, 1.0f);
        EXPECT (render (p, 48000.0, 0.5) > 0.0f, "renders");
        EXPECT (p.getEngine().isOversamplingActive(), "2x active");
        EXPECT (p.getLatencySamples() == latency, "latency unchanged");
    });

    run ("mono layout processes", [] {
        EvoxAudioProcessor p;
        p.setPlayConfigDetails (1, 1, 44100.0, 64);
        p.prepareToPlay (44100.0, 64);
        juce::AudioBuffer<float> buffer (1, 64);
        juce::MidiBuffer midi;
        const auto tone = tools::sawtooth (44100.0, 0.5, tools::constantPitch (220.0));
        bool finite = true;
        for (size_t pos = 0; pos + 64 <= tone.samples.size(); pos += 64)
        {
            buffer.copyFrom (0, 0, tone.samples.data() + pos, 64);
            p.processBlock (buffer, midi);
            for (int i = 0; i < 64; ++i)
                finite &= std::isfinite (buffer.getSample (0, i));
        }
        EXPECT (finite, "finite output");
    });

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PLUGIN TESTS PASSED" : "PLUGIN TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
