#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace evox::dsp
{

enum class ScaleType
{
    chromatic = 0,
    major,
    minor
};

/** Per-pitch-class override, matching the plugin's note0..note11 parameters. */
enum class NoteMode : std::uint8_t
{
    scale = 0, // follow Key + Scale
    enabled,
    removed,
    bypassed
};

/** Stage 3a: maps a continuous MIDI pitch to the target note of the active scale.

    - "Removed" notes are never targets: pitches snap to the nearest allowed neighbour.
    - "Bypassed" notes are left alone: when the nearest chromatic note is bypassed, no
      correction is applied at all.
    - Hysteresis: the current note is kept until the pitch is `hysteresis` semitones
      closer to another allowed note, which stops chattering between neighbours on
      pitches that sit near a boundary (e.g. a wide vibrato between two notes). */
class ScaleQuantizer
{
public:
    static constexpr int noTarget = -1000;   // nothing allowed
    static constexpr int bypassTarget = -999; // nearest note is bypassed

    void configure (int key, ScaleType scale, const std::array<NoteMode, 12>& modes) noexcept
    {
        static constexpr std::array<int, 7> majorSteps { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr std::array<int, 7> minorSteps { 0, 2, 3, 5, 7, 8, 10 };

        std::array<bool, 12> inScale {};
        if (scale == ScaleType::chromatic)
            inScale.fill (true);
        else
            for (int step : (scale == ScaleType::major ? majorSteps : minorSteps))
                inScale[static_cast<size_t> ((key + step) % 12)] = true;

        anyAllowed_ = false;
        for (size_t pc = 0; pc < 12; ++pc)
        {
            const auto mode = modes[pc];
            allowed_[pc] = mode == NoteMode::enabled || (mode == NoteMode::scale && inScale[pc]);
            bypassed_[pc] = mode == NoteMode::bypassed;
            anyAllowed_ = anyAllowed_ || allowed_[pc];
        }
    }

    void setHysteresis (float semitones) noexcept { hysteresis_ = semitones; }

    bool isAllowed (int note) const noexcept { return allowed_[pitchClass (note)]; }
    bool isBypassed (int note) const noexcept { return bypassed_[pitchClass (note)]; }

    /** @param midi    detected pitch on the (detuned) grid
        @param current previously chosen note, or noTarget
        @returns the MIDI note to aim for, bypassTarget, or noTarget. */
    int quantize (float midi, int current) const noexcept
    {
        const int nearestChromatic = static_cast<int> (std::lround (midi));
        if (isBypassed (nearestChromatic))
            return bypassTarget;
        if (! anyAllowed_)
            return noTarget;

        const int candidate = nearestAllowed (midi);

        if (current > noTarget + 1 && isAllowed (current) && candidate != current)
        {
            const float dCurrent = std::abs (midi - static_cast<float> (current));
            const float dCandidate = std::abs (midi - static_cast<float> (candidate));
            if (dCurrent < dCandidate + hysteresis_)
                return current;
        }
        return candidate;
    }

    int nearestAllowed (float midi) const noexcept
    {
        const int base = static_cast<int> (std::lround (midi));
        int best = noTarget;
        float bestDist = 1.0e9f;
        for (int offset = -6; offset <= 6; ++offset)
        {
            const int note = base + offset;
            if (! isAllowed (note))
                continue;
            const float d = std::abs (midi - static_cast<float> (note));
            if (d < bestDist)
            {
                bestDist = d;
                best = note;
            }
        }
        return best;
    }

private:
    static size_t pitchClass (int note) noexcept { return static_cast<size_t> (((note % 12) + 12) % 12); }

    std::array<bool, 12> allowed_ {};
    std::array<bool, 12> bypassed_ {};
    bool anyAllowed_ = false;
    float hysteresis_ = 0.15f;
};

} // namespace evox::dsp
