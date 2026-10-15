// Renders the EVOX editor to PNG with live analysis data, without a host or a window.
//   evox_snapshot out.png [width height scale] [--retune ms] [--expand-vibrato]
// Drives the real processor with a synthetic sung glide so the pitch display, correction
// meter, keyboard target and level meters all show real engine output.

#include "PluginEditor.h"
#include "common/SignalGen.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <typeinfo>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::String out = argc > 1 ? argv[1] : "evox.png";
    auto numberArg = [&] (int i, double fallback) { return argc > i && argv[i][0] != '-' ? std::atof (argv[i]) : fallback; };
    const int w = static_cast<int> (numberArg (2, 980));
    const int h = static_cast<int> (numberArg (3, 560));
    const auto scale = static_cast<float> (numberArg (4, 2.0));
    float retune = 0.0f;
    bool expandVibrato = false;
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--retune" && i + 1 < argc)
            retune = std::stof (argv[i + 1]);
        if (juce::String (argv[i]) == "--expand-vibrato")
            expandVibrato = true;
    }

    const double fs = 48000.0;
    const int block = 256;
    evox::EvoxAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, block);
    proc.prepareToPlay (fs, block);

    auto setParam = [&] (const juce::String& id, float plain) {
        auto* p = proc.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    setParam (evox::ParamID::retuneSpeed, retune);
    setParam (evox::ParamID::scale, 1.0f); // C major
    setParam (evox::ParamID::note (4), 3.0f); // bypass E, to show the state colours
    setParam (evox::ParamID::note (6), 1.0f); // force-enable F#

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setSize (w, h);
    auto* evoxEditor = dynamic_cast<evox::EvoxEditor*> (editor.get());
    evoxEditor->setVibratoExpanded (expandVibrato);

    // Sung glide with vibrato: C3 -> G3 -> D3 over ~4 s.
    auto up = evox::tools::logSweep (130.81, 196.0, 2.0);
    auto curve = evox::tools::withVibrato ([=] (double t) { return t < 2.0 ? up (t) : up (2.0 - (t - 2.0) * 0.7); }, 5.5, 30.0);
    const auto voice = evox::tools::synthVoice (fs, 3.9, curve);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + block <= voice.samples.size(); pos += block)
    {
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, voice.samples.data() + pos, block, 0.5f);
        proc.processBlock (buffer, midi);
        if ((pos / block) % 8 == 0)
            evoxEditor->tickForSnapshot(); // drains the FIFO like the 60 Hz timer
    }
    for (int i = 0; i < 4; ++i)
        evoxEditor->tickForSnapshot();

    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--bench-paint")
        {
            // Full-editor paint cost (worst case: everything repainted every frame).
            juce::Image target (juce::Image::ARGB, w, h, true);
            const int frames = 120;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int f = 0; f < frames; ++f)
            {
                juce::Graphics g (target);
                editor->paintEntireComponent (g, true);
            }
            const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / frames;
            std::printf ("full editor paint (%dx%d, software renderer): %.2f ms/frame = %.1f%% of a 60 fps frame budget\n",
                         w, h, ms, 100.0 * ms / (1000.0 / 60.0));

            // Per child: what actually repaints at 60 fps while audio plays.
            for (auto* child : editor->getChildren())
            {
                if (child->getWidth() < 2)
                    continue;
                juce::Image img (juce::Image::ARGB, child->getWidth(), child->getHeight(), true);
                const double c0 = juce::Time::getMillisecondCounterHiRes();
                for (int f = 0; f < frames; ++f)
                {
                    juce::Graphics g (img);
                    child->paintEntireComponent (g, true);
                }
                const double cms = (juce::Time::getMillisecondCounterHiRes() - c0) / frames;
                if (cms > 0.05)
                    std::printf ("  %-28s %4dx%-4d %.2f ms\n", typeid (*child).name(), child->getWidth(), child->getHeight(), cms);
            }
        }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (out));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);
    std::printf ("%s %s (%dx%d @%.1fx)\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8(), w, h, scale);
    editor.reset();
    return ok ? 0 : 1;
}
