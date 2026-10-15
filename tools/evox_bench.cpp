// Real-time CPU benchmark: renders a synthetic vocal through the full engine in
// host-sized blocks and reports the cost as % of one core in real time.
//   evox_bench [sampleRate=48000] [blockSize=128] [seconds=20]

#include "common/SignalGen.h"
#include "dsp/EvoxEngine.h"

#include <chrono>
#include <cstdio>
#include <string>

using namespace evox;

int main (int argc, char** argv)
{
    const double fs = argc > 1 ? std::stod (argv[1]) : 48000.0;
    const int block = argc > 2 ? std::stoi (argv[2]) : 128;
    const double seconds = argc > 3 ? std::stod (argv[3]) : 20.0;

    auto voice = tools::synthVoice (fs, 2.0, tools::withVibrato (tools::constantPitch (220.0), 5.5, 40.0));
    std::vector<float> left (static_cast<size_t> (block)), right (static_cast<size_t> (block));

    dsp::EvoxEngine engine;
    engine.prepare (fs, block, 2);

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
    std::printf ("%.0f Hz, %d-sample blocks, stereo: %.3f s CPU for %.1f s audio = %.2f%% of one core\n",
                 fs, block, elapsed, audio, 100.0 * elapsed / audio);
    return 0;
}
