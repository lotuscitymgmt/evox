#pragma once

// Minimal WAV reader/writer for offline tools and tests (never used on the audio thread).
// Reads 16/24/32-bit PCM and 32-bit float, including WAVE_FORMAT_EXTENSIBLE.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace evox::tools
{

struct WavData
{
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels;

    size_t numFrames() const { return channels.empty() ? 0 : channels[0].size(); }

    std::vector<float> mono() const
    {
        std::vector<float> out (numFrames(), 0.0f);
        for (const auto& ch : channels)
            for (size_t i = 0; i < out.size(); ++i)
                out[i] += ch[i] / static_cast<float> (channels.size());
        return out;
    }
};

inline bool readWav (const std::string& path, WavData& out, std::string& error)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
    {
        error = "cannot open " + path;
        return false;
    }

    std::vector<char> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    auto u16 = [&] (size_t p) { uint16_t v; std::memcpy (&v, bytes.data() + p, 2); return v; };
    auto u32 = [&] (size_t p) { uint32_t v; std::memcpy (&v, bytes.data() + p, 4); return v; };

    if (bytes.size() < 12 || std::memcmp (bytes.data(), "RIFF", 4) != 0 || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    uint16_t format = 0, numCh = 0, bits = 0;
    uint32_t rate = 0;
    size_t dataPos = 0, dataSize = 0;

    for (size_t p = 12; p + 8 <= bytes.size();)
    {
        const auto size = static_cast<size_t> (u32 (p + 4));
        if (std::memcmp (bytes.data() + p, "fmt ", 4) == 0)
        {
            format = u16 (p + 8);
            numCh = u16 (p + 10);
            rate = u32 (p + 12);
            bits = u16 (p + 22);
            if (format == 0xFFFE && size >= 26)
                format = u16 (p + 32); // sub-format GUID starts with the real format tag
        }
        else if (std::memcmp (bytes.data() + p, "data", 4) == 0)
        {
            dataPos = p + 8;
            dataSize = std::min (size, bytes.size() - dataPos);
        }
        p += 8 + size + (size & 1);
    }

    if (dataPos == 0 || numCh == 0 || ! (format == 1 || format == 3))
    {
        error = "unsupported WAV layout (need PCM or float)";
        return false;
    }

    const size_t bytesPerSample = bits / 8;
    const size_t frames = dataSize / (bytesPerSample * numCh);
    out.sampleRate = rate;
    out.channels.assign (numCh, std::vector<float> (frames));

    for (size_t i = 0; i < frames; ++i)
    {
        for (size_t c = 0; c < numCh; ++c)
        {
            const char* s = bytes.data() + dataPos + (i * numCh + c) * bytesPerSample;
            float v = 0.0f;
            if (format == 3 && bits == 32)
                std::memcpy (&v, s, 4);
            else if (bits == 16)
            {
                int16_t x; std::memcpy (&x, s, 2); v = x / 32768.0f;
            }
            else if (bits == 24)
            {
                int32_t x = (static_cast<uint8_t> (s[0])) | (static_cast<uint8_t> (s[1]) << 8) | (static_cast<int8_t> (s[2]) * 65536);
                v = static_cast<float> (x) / 8388608.0f;
            }
            else if (bits == 32)
            {
                int32_t x; std::memcpy (&x, s, 4); v = static_cast<float> (x / 2147483648.0);
            }
            out.channels[c][i] = v;
        }
    }
    return true;
}

inline bool writeWav (const std::string& path, const WavData& data)
{
    std::ofstream f (path, std::ios::binary);
    if (! f)
        return false;

    const auto numCh = static_cast<uint16_t> (data.channels.size());
    const auto frames = static_cast<uint32_t> (data.numFrames());
    const uint32_t rate = static_cast<uint32_t> (data.sampleRate);
    const uint32_t dataBytes = frames * numCh * 4;

    auto w32 = [&] (uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto w16 = [&] (uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };

    f.write ("RIFF", 4); w32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); w32 (16); w16 (3); w16 (numCh); w32 (rate);
    w32 (rate * numCh * 4); w16 (static_cast<uint16_t> (numCh * 4)); w16 (32);
    f.write ("data", 4); w32 (dataBytes);

    for (uint32_t i = 0; i < frames; ++i)
        for (const auto& ch : data.channels)
            f.write (reinterpret_cast<const char*> (&ch[i]), 4);

    return static_cast<bool> (f);
}

} // namespace evox::tools
