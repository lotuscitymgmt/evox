#pragma once

#include "common/SignalGen.h"
#include "dsp/PitchAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace evox::test
{

struct TrackStats
{
    int frames = 0;          // frames where ground truth is pitched (after warm-up)
    int voicedFrames = 0;    // ...of which the analyzer said voiced
    int octaveErrors = 0;    // |error| > 600 cents
    std::vector<float> absCents;

    float voicedRatio() const { return frames ? float (voicedFrames) / frames : 0.0f; }

    float percentile (float p) const
    {
        if (absCents.empty())
            return 1.0e9f;
        auto v = absCents;
        std::sort (v.begin(), v.end());
        return v[std::min (v.size() - 1, static_cast<size_t> (p * (v.size() - 1)))];
    }
};

/** Streams `sig` through a PitchAnalyzer and compares each estimate with the ground
    truth at the estimate's own window centre. */
inline TrackStats track (const tools::TestSignal& sig, double fs, dsp::InputType type, float tracking = 0.5f,
                         double warmupSeconds = 0.06, int* voicedOnUnpitched = nullptr, int* unpitchedFrames = nullptr)
{
    dsp::PitchAnalyzer analyzer;
    analyzer.prepare (fs);
    analyzer.setInputType (type);
    analyzer.setTracking (tracking);

    TrackStats stats;
    const auto warmup = static_cast<std::int64_t> (warmupSeconds * fs);

    for (size_t i = 0; i < sig.samples.size(); ++i)
    {
        if (! analyzer.push (sig.samples[i]))
            continue;

        const auto& e = analyzer.getEstimate();
        if (e.centreSample < warmup)
            continue;

        const float truth = sig.trueHz[static_cast<size_t> (e.centreSample)];
        if (truth <= 0.0f)
        {
            if (unpitchedFrames) ++*unpitchedFrames;
            if (voicedOnUnpitched && e.voiced) ++*voicedOnUnpitched;
            continue;
        }

        ++stats.frames;
        if (! e.voiced)
            continue;

        ++stats.voicedFrames;
        const float cents = std::abs (1200.0f * std::log2 (e.f0Hz / truth));
        stats.absCents.push_back (cents);
        if (cents > 600.0f)
            ++stats.octaveErrors;
    }
    return stats;
}

} // namespace evox::test
