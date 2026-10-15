// Offline pitch-analysis harness.
//   evox_analyze <input.wav> <output.csv> [--type soprano|alto|low|instrument] [--tracking 0..100]
//   evox_analyze --truth <truth.csv> ...   (optional) adds a ground-truth column for synthetic files
//
// Streams the file through the exact same InputConditioner + PitchAnalyzer the plugin
// uses, at the file's own sample rate and in plugin-sized blocks, and writes one CSV row
// per analysis hop. Plot with tools/plot_f0.py.

#include "common/WavFile.h"
#include "dsp/InputConditioner.h"
#include "dsp/PitchAnalyzer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace evox;

static dsp::InputType parseType (const std::string& s)
{
    if (s == "soprano") return dsp::InputType::soprano;
    if (s == "low") return dsp::InputType::lowMale;
    if (s == "instrument") return dsp::InputType::instrument;
    return dsp::InputType::altoTenor;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: %s in.wav out.csv [--type soprano|alto|low|instrument] [--tracking 0..100] [--no-lowcut]\n", argv[0]);
        return 2;
    }

    auto type = dsp::InputType::altoTenor;
    float tracking = 50.0f;
    bool lowCut = true;

    for (int i = 3; i < argc; ++i)
    {
        if (! std::strcmp (argv[i], "--type") && i + 1 < argc) type = parseType (argv[++i]);
        else if (! std::strcmp (argv[i], "--tracking") && i + 1 < argc) tracking = std::stof (argv[++i]);
        else if (! std::strcmp (argv[i], "--no-lowcut")) lowCut = false;
    }

    tools::WavData wav;
    std::string error;
    if (! tools::readWav (argv[1], wav, error))
    {
        std::fprintf (stderr, "error: %s\n", error.c_str());
        return 1;
    }

    const auto mono = wav.mono();
    dsp::InputConditioner conditioner;
    conditioner.prepare (wav.sampleRate);
    conditioner.setLowCutEnabled (lowCut);

    dsp::PitchAnalyzer analyzer;
    analyzer.prepare (wav.sampleRate);
    analyzer.setInputType (type);
    analyzer.setTracking (tracking / 100.0f);

    std::ofstream csv (argv[2]);
    csv << "time_s,f0_hz,midi,confidence,rms_db,zcr_hz,periodic,voiced\n";

    int frames = 0, voicedFrames = 0;
    for (size_t i = 0; i < mono.size(); ++i)
    {
        if (! analyzer.push (conditioner.process (mono[i])))
            continue;

        const auto& e = analyzer.getEstimate();
        const double t = static_cast<double> (e.centreSample) / wav.sampleRate;
        if (t < 0.0)
            continue;

        ++frames;
        voicedFrames += e.voiced ? 1 : 0;
        const float f0 = e.voiced ? e.f0Hz : 0.0f;
        char line[256];
        std::snprintf (line, sizeof line, "%.5f,%.3f,%.3f,%.4f,%.2f,%.1f,%d,%d\n", t, f0,
                       f0 > 0.0f ? dsp::hzToMidi (f0) : 0.0f, e.confidence, e.rmsDb, e.zcrHz,
                       e.periodic ? 1 : 0, e.voiced ? 1 : 0);
        csv << line;
    }

    std::printf ("%s: %.0f Hz, %.2f s, %d frames, %.1f%% voiced, window %d smp, hop %d smp -> %s\n",
                 argv[1], wav.sampleRate, mono.size() / wav.sampleRate, frames,
                 frames ? 100.0 * voicedFrames / frames : 0.0, analyzer.getDetector().getFrameLength(),
                 analyzer.getHopSizeHostSamples(), argv[2]);
    return 0;
}
