#pragma once

#include "CorrectionTrack.h"
#include "PitchMarker.h"

#include <array>
#include <cstdint>
#include <vector>

namespace evox::dsp
{

/** Stage 4b + 5: real-time TD-PSOLA pitch shifter with grain-resampling formant control.

    Timeline: input sample n is written at absolute index n ("now"). The output for index
    p = now - latency is produced by overlap-adding grains:

      - Synthesis marks t_s advance by T_out = T_in / ratio (ratio from the CorrectionTrack
        entry for t_s, so a decision lands on the audio it was measured on). A ratio step is
        therefore an immediate change of grain spacing - no glide is introduced here.
      - Each synthesis grain copies the analysis grain around the pitch mark t_a closest to
        t_s *whose right edge is already available* (t_a + H <= now). For a periodic voice,
        reaching back a whole period is inaudible, which is what lets a 9 ms lookahead
        serve periods longer than 9 ms.
      - Grains are windowed with hann(x)^k over +/- H around the mark. Character blends from
        clean (H = 1.0 T, k = 1: classic 2-period Hann, smooth COLA) to hard (H = 0.65 T,
        k = 3: short, peaky grains -> pitch-synchronous buzz, the glassy edge).
      - Formants: a grain read at speed 1/alpha is stretched by alpha, which scales its
        spectral envelope by 1/alpha while the pitch stays set by the grain spacing.
        alpha = throat (formant correction on), or throat / ratio (off -> "chipmunk").
        The output grain length depends only on Character; alpha sets how much source it reads (2H/alpha).
      - Fractional positions on both sides use 4-point Hermite interpolation.
      - Presence shelf: short, peaky grains smooth the spectral envelope and cost ~2-3 dB
        above 3 kHz. A Character-linked high shelf (3.5 kHz, 0 -> +5 dB) more than gives it
        back, so harder settings end up net brighter - crisp, not dull.

    Real-time safe after prepare(). */
class PsolaShifter
{
public:
    static constexpr int maxChannels = 2;

    struct Settings
    {
        float character = 0.7f;     // 0 clean .. 1 hard
        bool formantCorrection = true;
        float throat = 1.0f;        // 0.5 .. 1.8 (vocal-tract length factor)
    };

    void prepare (double sampleRate, int latencySamples, float lowestHz);
    void reset() noexcept;
    void setSettings (const Settings& s) noexcept;

    /** Write one input frame at index `now` and update pitch marks. */
    void pushInput (const float* channels, int numChannels, float mono, std::int64_t now,
                    float detectedPeriod, bool voiced) noexcept;

    /** Produce the shifted output for index p = now - latency into out[0..numChannels). */
    void render (std::int64_t p, const CorrectionTrack& track, float* out, int numChannels) noexcept;

    /** Unshifted (conditioned) input at index p, for the unvoiced/dry crossfade. */
    float readInput (int channel, std::int64_t p) const noexcept
    {
        return input_[static_cast<size_t> (channel)][static_cast<size_t> (p & inputMask_)];
    }

    const PitchMarker& getMarker() const noexcept { return marker_; }

    /** Diagnostics for tests: synthesis marks placed so far and their positions. */
    std::int64_t getGrainCount() const noexcept { return grains_; }
    double getLastSynthesisMark() const noexcept { return lastSynthesis_; }

private:
    void rebuildWindow (float character) noexcept;
    float window (float x) const noexcept;
    float grainStretch (float ratio) const noexcept;
    float sourceHalfLength (float period, float alpha) const noexcept;
    float readHermite (int channel, double position) const noexcept;
    void placeGrain (double ts, float period, float ratio, std::int64_t p, int numChannels) noexcept;

    static constexpr int windowSize = 1024;

    double sampleRate_ = 48000.0;
    int latency_ = 0;
    Settings settings_;
    float appliedCharacter_ = -1.0f;
    float halfFactor_ = 1.0f, windowArea_ = 1.0f;
    std::array<float, windowSize + 2> windowTable_ {};

    PitchMarker marker_;
    std::array<Biquad, maxChannels> presence_;
    std::array<std::vector<float>, maxChannels> input_, accum_;
    std::int64_t inputMask_ = 0, accumMask_ = 0;

    CorrectionTrack::Cursor cursor_;
    double nextSynthesis_ = 0.0, lastSynthesis_ = 0.0;
    std::int64_t now_ = 0, markSearch_ = 0, grains_ = 0;
    bool started_ = false;
    float maxPeriod_ = 1000.0f;
};

} // namespace evox::dsp
