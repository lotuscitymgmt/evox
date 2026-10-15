// Renders the synthetic benchmark set (WAV + ground-truth CSV) used to eyeball the tracker.
//   evox_gensignals <output-dir> [sampleRate]

#include "common/SignalGen.h"
#include "common/WavFile.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace evox::tools;

static void save (const std::string& dir, const std::string& name, const TestSignal& s, double fs)
{
    WavData wav;
    wav.sampleRate = fs;
    wav.channels = { s.samples };
    writeWav (dir + "/" + name + ".wav", wav);

    std::ofstream csv (dir + "/" + name + ".truth.csv");
    csv << "time_s,f0_hz\n";
    const size_t step = static_cast<size_t> (fs / 1000.0); // 1 ms resolution
    for (size_t i = 0; i < s.trueHz.size(); i += step)
        csv << static_cast<double> (i) / fs << "," << s.trueHz[i] << "\n";
    std::printf ("wrote %s/%s.wav\n", dir.c_str(), name.c_str());
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "signals";
    const double fs = argc > 2 ? std::stod (argv[2]) : 48000.0;
    std::filesystem::create_directories (dir);

    save (dir, "sine_sweep_80_800", sine (fs, 4.0, logSweep (80.0, 800.0, 4.0)), fs);
    save (dir, "saw_sweep_55_400", sawtooth (fs, 4.0, logSweep (55.0, 400.0, 4.0)), fs);

    // A stepped "melisma": 16th-note run at 150 bpm (100 ms per note) around A3.
    const double run[] = { 220.0, 246.9, 261.6, 293.7, 329.6, 293.7, 261.6, 246.9, 220.0, 196.0, 220.0, 246.9 };
    auto runCurve = withVibrato ([&] (double t) { return run[static_cast<size_t> (t / 0.1) % 12]; }, 5.0, 15.0);
    save (dir, "voice_melisma_A3", synthVoice (fs, 2.4, runCurve), fs);

    save (dir, "voice_vibrato_G2", synthVoice (fs, 3.0, withVibrato (constantPitch (98.0), 5.5, 60.0)), fs);
    save (dir, "voice_breathy_D4", synthVoice (fs, 3.0, constantPitch (293.7), 0.3f), fs);
    save (dir, "voice_s_voice", concat ({ synthVoice (fs, 0.5, constantPitch (196.0)), sibilant (fs, 0.3, 0.2f),
                                          synthVoice (fs, 0.5, constantPitch (261.6), 0.02f, 4) }), fs);
    return 0;
}
