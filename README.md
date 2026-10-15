# EVOX — Lotus Sounds

Real-time vocal pitch correction tuned for the mid/late-2000s hard-tune sound: stepped
note jumps at fast retune speeds, a bright, slightly glassy TD-PSOLA texture, and tight,
low-latency tracking. Clean-room design built on published DSP techniques (YIN,
TD-PSOLA, LPC/grain-resampling formant handling).

**Status:** Phase 1 (skeleton, parameters, bypass, latency) and Phase 2 (input
conditioning + pitch detection, test harness) are complete. The plugin currently passes
audio through with latency compensation, analyses pitch live, and shows the detected note
in a temporary developer UI. Pitch correction lands in Phase 3.

---

## Building

Requirements: CMake ≥ 3.22, a C++20 compiler (Xcode 14+, Visual Studio 2022, GCC 11+/Clang 14+),
Git. JUCE 8.0.15 is fetched automatically.

```bash
# macOS (universal arm64 + x86_64: VST3, AU, Standalone)
cmake -B build -G Xcode
cmake --build build --config Release

# Windows (VST3, Standalone)
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

# Linux (VST3, Standalone) — needs: libasound2-dev libx11-dev libxrandr-dev libxinerama-dev
#   libxcursor-dev libxext-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Use an existing JUCE checkout instead of downloading: `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

Artefacts land in `build/EVOX_artefacts/Release/{VST3,AU,Standalone}`. Copy them to
`~/Library/Audio/Plug-Ins/{VST3,Components}` (macOS) or `C:\Program Files\Common Files\VST3`
(Windows), or set `COPY_PLUGIN_AFTER_BUILD TRUE` in `CMakeLists.txt`.
On macOS, run `auval -v aufx Evox Ltsd` after installing the AU.

### Tests, harness, benchmark (no JUCE needed)

```bash
cmake -B build-tests -DEVOX_BUILD_PLUGIN=OFF && cmake --build build-tests
ctest --test-dir build-tests --output-on-failure      # or ./build-tests/tests/evox_tests [filter]

# Offline pitch harness: any WAV in, CSV of f0/confidence/voicing out, PNG plot
./build-tests/tools/evox_gensignals tools/out/signals 48000          # synthetic benchmark set + ground truth
./build-tests/tools/evox_analyze my_vocal.wav out.csv --type alto --tracking 50
python3 tools/plot_f0.py out.csv [--truth file.truth.csv] -o out.png  # needs matplotlib

# CPU benchmark of the full engine
./build-tests/tools/evox_bench 48000 128 20
```

`--type` is `soprano | alto | low | instrument`. Drop your own reference vocals (era
benchmarks, dry acapellas) through `evox_analyze` to see exactly what the tracker sees.

### pluginval

```bash
pluginval --strictness-level 5 --validate build/EVOX_artefacts/Release/VST3/EVOX.vst3
```

---

## Architecture

```
source/dsp/      Pure C++20 DSP core — no JUCE. Shared by plugin, tests, tools, future AAX.
  InputConditioner   DC blocker (5 Hz) + optional 60 Hz 2nd-order Butterworth high-pass
  VoicingDetector    energy gate + zero-crossing rate + YIN periodicity, with hysteresis
  Decimator          analysis-only anti-aliased decimation to 20–25 kHz (6th-order Butterworth)
  YinPitchDetector   FFT-based YIN, CMNDF, absolute threshold, parabolic interpolation,
                     subharmonic (octave-up error) check
  PitchAnalyzer      streaming wrapper: hop scheduling, octave-jump rejection ("Tracking"),
                     voicing, timestamps each estimate with the input sample it describes
  EvoxEngine         owns all stages, latency budget, dry/wet, gain, bypass crossfade,
                     publishes AnalysisFrames to the UI through a lock-free SPSC FIFO
  Fft, Biquad, DelayLine, Smoother, SpscFifo   real-time-safe building blocks
source/plugin/   JUCE wrapper: APVTS parameters, processor, (temporary) editor
tests/           dependency-free unit tests (incl. an allocation probe on process())
tools/           offline harness, signal generator, CPU benchmark, plotting script
```

Real-time rules (enforced by review + tests): `prepare()` allocates, `process()` never
allocates, locks or does I/O. `tests/test_engine.cpp` replaces global `operator new` and
fails if `EvoxEngine::process()` allocates once across parameter changes and odd block sizes.

The DSP core takes a plain `EngineParams` struct per block, so an AAX wrapper only needs to
fill that struct — no JUCE types cross the boundary.

### Pitch detection details

* **YIN** (de Cheveigné & Kawahara 2002). The difference function is computed in
  O(N log N): both correlation operands are packed into the real/imag parts of a single
  complex FFT, and the energy terms come from prefix sums.
* **Newest-first framing.** YIN only integrates over the first `W + period` samples of a frame,
  so the frame is fed newest-first. Estimates then describe the most recent audio, not the
  oldest. On fast glides this cut the median tracking error from ~8 ct to ~1–2 ct.
* **Window per Input Type** (analysis rate ≈ 24 kHz; window = W + τmax):

  | Input Type | f0 range | analysis frame |
  |---|---|---|
  | Soprano    | 140–1100 Hz | ~14 ms |
  | Alto/Tenor | 80–800 Hz   | ~25 ms |
  | Low Male   | 55–400 Hz   | ~36 ms |
  | Instrument | 45–2000 Hz  | ~45 ms |

  Hop = 64 analysis samples (~2.7 ms), so a new estimate arrives every 2.7 ms whatever
  the window length.
* **Tracking** (0 = tight … 100 = loose) moves the YIN threshold (0.10 → 0.35), the
  octave-jump margin and confirmation length (tight: an octave leap must persist ~5 hops
  ≈ 13 ms before it is believed), the energy gate (−50 → −60 dBFS) and the ZCR headroom.

### Measured (synthetic, see `tests/test_pitch.cpp`)

* Steady sines/saws 50–1760 Hz at 44.1/48/96 kHz: p95 error < 2 ct (< 5 ct above 1.2 kHz).
* Log sweeps up and down, ~1.5 oct/s: median ≈ 1–2 ct, zero octave errors.
* Synthetic voice with 50 ct vibrato, jitter, shimmer and breath: median < 5 ct, zero octave errors.
* Fundamental 12 dB under the 2nd harmonic: zero octave errors at tight and medium tracking.
* White noise, "sss" noise and silence: ≤ 2% of frames voiced.
* 100 ms-per-note melisma: the tracker locks onto each new note within 1–2 hops (see the
  `voice_melisma_A3` plot from the harness).

---

## Latency

Reported latency = **9.0 ms** (397 samples at 44.1 kHz, 432 at 48 kHz). It is constant for
the session, so changing the Input Type never makes the host recompensate.

This is the lookahead budget for Phase 3's TD-PSOLA. A two-period grain centred on a pitch
mark needs one period of "future" audio, and 9 ms covers a full period down to ~111 Hz.
Below that (deep Low Male notes) the shifter will use asymmetric grains whose right half is
trimmed to the available lookahead. This is a deliberate trade to stay under the ~10 ms
target. The alternative, a ~18 ms latency for Low Male only, is easy to switch to if you
prefer quality over latency on bass voices.

Pitch analysis itself adds no audio latency. It runs on the undelayed input, so each estimate
is computed before the audio it describes reaches the (delayed) synthesis point.

---

## Parameters (all in APVTS, automatable, saved with state)

| ID | Range | Default | Notes |
|---|---|---|---|
| retuneSpeed | 0–400 ms (skewed, centre 50) | 0 ms | **not smoothed**: 0 ms = instant step |
| humanize | 0–100 | 0 | |
| tracking | 0–100 | 50 | 0 tight … 100 loose |
| naturalVibrato | ±12 dB | 0 | |
| inputType | Soprano / Alto-Tenor / Low Male / Instrument | Alto/Tenor | |
| key, scale | C…B; Chromatic / Major / Minor | C, Chromatic | |
| note0…note11 | Scale / Enabled / Removed / Bypass | Scale | per-pitch-class override |
| transpose | ±24 st | 0 | |
| detune | ±100 ct | 0 | |
| referencePitch | 415–465 Hz | 440 | |
| vibratoOn / Shape / Rate / Depth / Delay | Sine/Square/Saw, 0.1–10 Hz, 0–200 ct, 0–2000 ms | off, sine, 5.5, 30, 300 | |
| character | 0–100 | 70 | clean → hard grains |
| formantOn | bool | on | |
| throat | 50–180 % | 100 | |
| mix | 0–100 % | 100 | smoothed |
| outputGain | −24…+12 dB | 0 | smoothed |
| oversampling | 1x / 2x | 1x | character stage |
| lowCut | bool | on | 60 Hz HPF |
| bypass | bool | off | exposed as the host bypass parameter; 10 ms crossfade |

Parameter IDs are versioned (`ParameterID{id, 1}`) and must never be renamed.

---

## Licensing notes

* **JUCE 8** is dual-licensed: AGPLv3, or a commercial JUCE licence (including the free
  "Starter" tier below its revenue cap). Selling closed-source EVOX requires a commercial
  JUCE licence for Lotus Sounds. Check the current tiers at juce.com before release.
* **VST3**: the Steinberg SDK bundled with JUCE is used under its own licence terms.
* **AAX** (later): needs the Avid AAX SDK plus a PACE/iLok signing agreement.
* No third-party DSP libraries are used. The FFT, filters, YIN and test framework are written
  in-house.
