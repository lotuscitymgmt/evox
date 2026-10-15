// Real-time CPU benchmark: renders a synthetic vocal through the full engine in
// host-sized blocks and reports the cost as % of one core in real time.
//   evox_bench [sampleRate=48000] [blockSize=128] [seconds=20] [--os2x] [--worst]
// --worst: Low Male range (longest analysis window), Throat 55% (longest source reads),
//          Character 0 (longest grains), +12 st (densest grains), vibrato on.

#include "common/SignalGen.h"
#include "dsp/EvoxEngine.h"

#include <chrono>
#include <cstdio>
#include <string>

using namespace evox;

int main (int argc, char** argv)
{
    auto positional = [&] (int i, double fallback) { return argc > i && argv[i][0] != '-' ? std::stod (argv[i]) : fallback; };
    const double fs = positional (1, 48000.0);
    const int block = static_cast<int> (positional (2, 128));
    const double seconds = positional (3, 20.0);

    auto voice = tools::synthVoice (fs, 2.0, tools::withVibrato (tools::constantPitch (220.0), 5.5, 40.0));
    std::vector<float> left (static_cast<size_t> (block)), right (static_cast<size_t> (block));

    dsp::EvoxEngine engine;
    engine.prepare (fs, block, 2);

    dsp::EngineParams params;
    std::string label = "default";
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--os2x")
        {
            params.oversample2x = true;
            label += "+2x";
        }
        if (a == "--worst")
        {
            params.inputType = dsp::InputType::lowMale;
            params.shifter.throat = 0.55f;
            params.shifter.character = 0.0f;
            params.correction.transpose = 12.0f;
            params.correction.vibratoOn = true;
            label = "worst-case" + std::string (params.oversample2x ? "+2x" : "");
        }
    }
    engine.setParams (params);

    const auto totalBlocks = static_cast<long> (seconds * fs / block);
    size_t pos = 0;
    const auto start = std::chrono::steady_clock::now();

    for (long b = 0; b < totalBlocks; ++b)
    {
        for (int i = 0; i < block; ++i, pos = (pos + 1) % voice.samples.size())
            left[static_cast<size_t> (i)] = right[static_cast<size_t> (i)] = voice.samples[pos];

        float* chans[2] = { left.data(), right.data() };
        engine.process (chans, 2, block);
    }

    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    const double audio = static_cast<double> (totalBlocks) * block / fs;
    std::printf ("%-16s %.0f Hz, %4d-sample blocks, stereo: %.2f%% of one core\n",
                 label.c_str(), fs, block, 100.0 * elapsed / audio);
    return 0;
}
