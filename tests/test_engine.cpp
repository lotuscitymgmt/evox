#include "TestFramework.h"

#include "common/SignalGen.h"
#include "dsp/EvoxEngine.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

// --- Allocation probe: counts every global operator new while armed. ---------------
namespace
{
std::atomic<bool> probeArmed { false };
std::atomic<int> probeCount { 0 };
} // namespace

void* operator new (std::size_t size)
{
    if (probeArmed.load (std::memory_order_relaxed))
        probeCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

using namespace evox;

namespace
{
void runBlocks (dsp::EvoxEngine& engine, std::vector<std::vector<float>>& audio, int blockSize, bool hostBypass = false)
{
    const int total = static_cast<int> (audio[0].size());
    for (int start = 0; start < total; start += blockSize)
    {
        const int n = std::min (blockSize, total - start);
        float* ptrs[2] = { audio[0].data() + start, audio.size() > 1 ? audio[1].data() + start : nullptr };
        if (hostBypass)
            engine.processBypassed (ptrs, static_cast<int> (audio.size()), n);
        else
            engine.process (ptrs, static_cast<int> (audio.size()), n);
    }
}

int peakIndex (const std::vector<float>& v)
{
    int best = 0;
    for (int i = 1; i < static_cast<int> (v.size()); ++i)
        if (std::abs (v[static_cast<size_t> (i)]) > std::abs (v[static_cast<size_t> (best)]))
            best = i;
    return best;
}
} // namespace

TEST_CASE ("engine: latency is under 10 ms and reported consistently")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        dsp::EvoxEngine e;
        e.prepare (fs, 512, 2);
        CHECK (e.getLatencySamples() == dsp::EvoxEngine::latencyFor (fs));
        CHECK (1000.0 * e.getLatencySamples() / fs < 10.0);
    }
}

TEST_CASE ("engine: impulse emerges exactly at the reported latency (all paths)")
{
    const double fs = 48000.0;

    for (int mode = 0; mode < 3; ++mode) // 0 = processing, 1 = bypass param, 2 = host bypass
    {
        dsp::EvoxEngine e;
        e.prepare (fs, 256, 2);
        dsp::EngineParams p;
        p.lowCut = false;
        p.bypass = mode == 1;
        e.setParams (p);

        std::vector<std::vector<float>> audio (2, std::vector<float> (4096, 0.0f));
        audio[0][100] = 1.0f;
        audio[1][100] = -0.5f;
        runBlocks (e, audio, 256, mode == 2);

        CHECK_MSG (peakIndex (audio[0]) == 100 + e.getLatencySamples(), "mode " + std::to_string (mode));
        CHECK_MSG (peakIndex (audio[1]) == 100 + e.getLatencySamples(), "mode " + std::to_string (mode));
        if (mode > 0)
        {
            CHECK_NEAR (audio[0][static_cast<size_t> (100 + e.getLatencySamples())], 1.0f, 1.0e-6f);
            CHECK_NEAR (audio[1][static_cast<size_t> (100 + e.getLatencySamples())], -0.5f, 1.0e-6f);
        }
    }
}

TEST_CASE ("engine: output gain applies to the latency-aligned dry path at mix 0")
{
    const double fs = 48000.0;
    dsp::EvoxEngine e;
    e.prepare (fs, 256, 1);
    dsp::EngineParams p;
    p.lowCut = false;
    p.mix = 0.0f;
    p.outputGainDb = -6.0206f;
    e.setParams (p);

    auto sig = tools::sine (fs, 0.5, tools::constantPitch (1000.0), 0.5f);
    std::vector<std::vector<float>> audio { sig.samples };
    runBlocks (e, audio, 128);

    const int lat = e.getLatencySamples();
    double err = 0.0;
    for (size_t i = 10000; i < audio[0].size(); ++i)
        err = std::max (err, std::abs (double (audio[0][i]) - 0.5 * sig.samples[i - static_cast<size_t> (lat)]));
    CHECK (err < 1.0e-5);
}

TEST_CASE ("engine: process() performs zero heap allocations")
{
    const double fs = 48000.0;
    dsp::EvoxEngine e;
    e.prepare (fs, 512, 2);

    auto voice = tools::synthVoice (fs, 1.0, tools::constantPitch (220.0));
    std::vector<std::vector<float>> audio { voice.samples, voice.samples };

    probeCount = 0;
    probeArmed = true;
    for (int it = 0; it < 4; ++it)
    {
        dsp::EngineParams p;
        p.inputType = static_cast<dsp::InputType> (it);
        p.tracking = 0.25f * it;
        p.mix = 1.0f - 0.2f * it;
        p.bypass = it == 3;
        p.oversample2x = it % 2 == 1;
        e.setParams (p);
        runBlocks (e, audio, 128);
        runBlocks (e, audio, 37, true);
    }
    probeArmed = false;
    CHECK_MSG (probeCount.load() == 0, std::to_string (probeCount.load()) + " allocations");
}

TEST_CASE ("engine: analysis frames reach the UI FIFO and report pitch")
{
    const double fs = 44100.0;
    dsp::EvoxEngine e;
    e.prepare (fs, 512, 1);
    auto sig = tools::sawtooth (fs, 0.5, tools::constantPitch (330.0));
    std::vector<std::vector<float>> audio { sig.samples };
    runBlocks (e, audio, 512);

    int frames = 0, voiced = 0;
    dsp::AnalysisFrame f;
    while (e.getAnalysisFifo().pop (f))
    {
        ++frames;
        if (f.voiced && std::abs (dsp::centsBetween (330.0f, f.inputHz)) < 3.0f)
            ++voiced;
    }
    CHECK (frames > 150);
    CHECK (voiced > frames * 9 / 10);
}

TEST_CASE ("fifo: SPSC transfers in order across threads")
{
    static dsp::SpscFifo<int, 64> fifo;
    constexpr int count = 200000;
    std::thread producer ([] {
        for (int i = 0; i < count;)
            if (fifo.push (i))
                ++i;
    });

    int expected = 0;
    bool ordered = true;
    while (expected < count)
    {
        int v;
        if (fifo.pop (v))
        {
            ordered &= v == expected;
            ++expected;
        }
    }
    producer.join();
    CHECK (ordered);
}
