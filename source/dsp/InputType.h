#pragma once

#include <cmath>

namespace evox::dsp
{

/** Voice classes for the "Input Type" selector. Each sets the f0 search range,
    which in turn sizes the analysis window (longest period => longest window). */
enum class InputType
{
    soprano = 0,
    altoTenor,
    lowMale,
    instrument
};

struct PitchRange
{
    float minHz;
    float maxHz;
};

constexpr PitchRange pitchRangeFor (InputType type) noexcept
{
    switch (type)
    {
        case InputType::soprano:    return { 140.0f, 1100.0f };
        case InputType::altoTenor:  return { 80.0f, 800.0f };
        case InputType::lowMale:    return { 55.0f, 400.0f };
        case InputType::instrument: return { 45.0f, 2000.0f };
    }
    return { 80.0f, 800.0f };
}

inline float hzToMidi (float hz, float referenceA4 = 440.0f) noexcept
{
    return 69.0f + 12.0f * std::log2 (hz / referenceA4);
}

inline float midiToHz (float midi, float referenceA4 = 440.0f) noexcept
{
    return referenceA4 * std::exp2 ((midi - 69.0f) / 12.0f);
}

inline float centsBetween (float hzA, float hzB) noexcept
{
    return 1200.0f * std::log2 (hzB / hzA);
}

} // namespace evox::dsp
