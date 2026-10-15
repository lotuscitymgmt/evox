#pragma once

namespace evox::dsp
{

/** Stage 1b: voiced / unvoiced classifier.

    A frame is a voiced *candidate* when all three cues agree:
      - energy above the gate (rejects silence and room noise),
      - zero-crossing rate below a pitch-dependent ceiling (rejects s / sh / t / breaths,
        which are noise-like and cross zero thousands of times per second) - unless the
        frame is so strongly periodic that it cannot be a consonant,
      - the pitch detector found a periodic dip below its confidence threshold.

    Hysteresis (onset/release frame counts) stops the classifier flickering on
    consonant-vowel boundaries. The engine turns the boolean into a short crossfade. */
class VoicingDetector
{
public:
    struct Features
    {
        float rmsDb = -120.0f;
        float zcrHz = 0.0f;   // zero crossings per second / 2 (i.e. an equivalent frequency)
        float f0Hz = 0.0f;
        bool periodic = false;
        float confidence = 0.0f; // 1 - YIN aperiodicity
    };

    void setGateDb (float db) noexcept { gateDb_ = db; }
    /** Extra ZCR headroom for breathy voices (raised as Tracking loosens). */
    void setZcrHeadroomHz (float hz) noexcept { zcrHeadroomHz_ = hz; }
    void setOnsetFrames (int n) noexcept { onsetFrames_ = n; }
    void setReleaseFrames (int n) noexcept { releaseFrames_ = n; }

    void reset() noexcept
    {
        voiced_ = false;
        counter_ = 0;
    }

    float zcrCeilingHz (float f0Hz) const noexcept { return 2200.0f + f0Hz + zcrHeadroomHz_; }

    /** Above this confidence the waveform is unambiguously periodic and the ZCR cue is
        ignored: breath noise on a sung vowel raises ZCR without making it a consonant. */
    static constexpr float zcrOverrideConfidence = 0.9f;

    bool isCandidate (const Features& f) const noexcept
    {
        const bool zcrOk = f.zcrHz < zcrCeilingHz (f.f0Hz) || f.confidence >= zcrOverrideConfidence;
        return f.periodic && f.rmsDb > gateDb_ && zcrOk;
    }

    bool update (const Features& f) noexcept
    {
        const bool candidate = isCandidate (f);

        if (candidate == voiced_)
        {
            counter_ = 0;
        }
        else if (++counter_ >= (candidate ? onsetFrames_ : releaseFrames_))
        {
            voiced_ = candidate;
            counter_ = 0;
        }

        return voiced_;
    }

    bool isVoiced() const noexcept { return voiced_; }

private:
    float gateDb_ = -50.0f, zcrHeadroomHz_ = 0.0f;
    int onsetFrames_ = 2, releaseFrames_ = 3, counter_ = 0;
    bool voiced_ = false;
};

} // namespace evox::dsp
