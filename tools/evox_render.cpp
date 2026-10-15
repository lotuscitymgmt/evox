// Offline renderer: runs a WAV through the complete EVOX engine.
//   evox_render in.wav out.wav [--retune ms] [--humanize 0..100] [--key C..B] [--scale chromatic|major|minor]
//               [--transpose st] [--type soprano|alto|low|instrument] [--character 0..100]
//               [--throat 50..180] [--formant on|off] [--vibrato sine|square|saw] [--mix 0..100]

#include "common/WavFile.h"
#include "dsp/EvoxEngine.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace evox;

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: %s in.wav out.wav [options] (see source header)\n", argv[0]);
        return 2;
    }

    dsp::EngineParams p;
    static const char* keys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    for (int i = 3; i + 1 < argc; i += 2)
    {
        const std::string opt = argv[i], val = argv[i + 1];
        if (opt == "--retune") p.correction.retuneMs = std::stof (val);
        else if (opt == "--humanize") p.correction.humanize = std::stof (val) / 100.0f;
        else if (opt == "--transpose") p.correction.transpose = std::stof (val);
        else if (opt == "--character") p.shifter.character = std::stof (val) / 100.0f;
        else if (opt == "--throat") p.shifter.throat = std::stof (val) / 100.0f;
        else if (opt == "--formant") p.shifter.formantCorrection = val == "on";
        else if (opt == "--mix") p.mix = std::stof (val) / 100.0f;
        else if (opt == "--scale") p.correction.scale = val == "major" ? dsp::ScaleType::major : val == "minor" ? dsp::ScaleType::minor : dsp::ScaleType::chromatic;
        else if (opt == "--type") p.inputType = val == "soprano" ? dsp::InputType::soprano : val == "low" ? dsp::InputType::lowMale
                                              : val == "instrument" ? dsp::InputType::instrument : dsp::InputType::altoTenor;
        else if (opt == "--key")
        {
            for (int k = 0; k < 12; ++k)
                if (val == keys[k]) p.correction.key = k;
        }
        else if (opt == "--vibrato")
        {
            p.correction.vibratoOn = true;
            p.correction.vibratoShape = val == "square" ? dsp::VibratoShape::square : val == "saw" ? dsp::VibratoShape::saw : dsp::VibratoShape::sine;
        }
    }

    tools::WavData wav;
    std::string error;
    if (! tools::readWav (argv[1], wav, error))
    {
        std::fprintf (stderr, "error: %s\n", error.c_str());
        return 1;
    }
    if (wav.channels.size() > 2)
        wav.channels.resize (2);

    dsp::EvoxEngine engine;
    const int block = 128;
    engine.prepare (wav.sampleRate, block, static_cast<int> (wav.channels.size()));
    engine.setParams (p);

    const auto total = wav.numFrames();
    const auto latency = static_cast<size_t> (engine.getLatencySamples());
    for (auto& ch : wav.channels)
        ch.resize (total + latency, 0.0f); // flush the lookahead

    for (size_t start = 0; start < total + latency; start += block)
    {
        const int n = static_cast<int> (std::min<size_t> (block, total + latency - start));
        float* ptrs[2] = { wav.channels[0].data() + start, wav.channels.size() > 1 ? wav.channels[1].data() + start : nullptr };
        engine.process (ptrs, static_cast<int> (wav.channels.size()), n);
    }

    for (auto& ch : wav.channels) // remove the latency so output lines up with the input file
        ch.erase (ch.begin(), ch.begin() + static_cast<long> (latency));

    tools::writeWav (argv[2], wav);
    std::printf ("rendered %s -> %s (%.2f s, latency %zu smp compensated)\n", argv[1], argv[2], total / wav.sampleRate, latency);
    return 0;
}
