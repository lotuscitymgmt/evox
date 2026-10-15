# EVOX — Lotus Sounds

Real-time vocal pitch correction tuned for the mid/late-2000s hard-tune sound: stepped
note jumps at fast retune speeds, a bright, slightly glassy TD-PSOLA texture, and tight,
low-latency tracking. Clean-room design built on published DSP techniques (YIN,
TD-PSOLA, LPC/grain-resampling formant handling).

**Status:** Phases 1–4 are complete: skeleton/parameters/latency, pitch detection,
correction logic + TD-PSOLA shifter, formant/throat control. Every parameter except
oversampling is live. The UI is still the temporary developer editor (full UI = Phase 5).
Oversampling, presets, profiling on target hardware and the final README are Phase 6.

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

# Offline renderer: any WAV through the full engine (latency removed so files line up)
./build-tests/tools/evox_render vocal.wav tuned.wav --retune 0 --key A --scale minor --character 80
#   other options: --humanize --transpose --throat --formant on|off --vibrato sine|square|saw --mix --type

# Offline pitch harness: any WAV in, CSV of f0/confidence/voicing out, PNG plot
./build-tests/tools/evox_gensignals tools/out/signals 48000          # synthetic benchmark set + ground truth
./build-tests/tools/evox_analyze my_vocal.wav out.csv --type alto --tracking 50
python3 tools/plot_f0.py out.csv [--truth file.truth.csv] -o out.png  # needs matplotlib
python3 tools/plot_f0.py in.csv --output-track tuned.csv -o compare.png  # input (cyan) vs output (pink)

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
  ScaleQuantizer     key/scale + per-note enable/remove/bypass, hysteresis
  PitchCorrector     retune (time constant on the correction offset), humanize, natural
                     vibrato scaling, create-vibrato LFO, transpose, detune, reference pitch
  CorrectionTrack    timestamped decisions so each lands on the audio it was measured on
  PitchMarker        glottal-peak pitch marks, phase-coherent from period to period
  PsolaShifter       TD-PSOLA synthesis, Character windows, grain-resampling formants/throat,
                     presence shelf
  EvoxEngine         owns all stages, latency budget, voiced/unvoiced crossfade, level match,
                     dry/wet, gain, bypass crossfade; publishes AnalysisFrames to the UI
                     through a lock-free SPSC FIFO
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

## Correction and shifting (Phases 3–4)

**Correction model.** `output = input + c`, and Retune Speed is the time constant with which
the correction `c` chases `target − input`. At 0 ms `c` is assigned exactly on every 2.7 ms
analysis step: the output is the target note, flat, and it changes the instant the target
does. Nothing in the chain smooths it. The shifter applies a ratio change at the very next
grain, so the jump is a true step. At slower speeds the singer's own movement passes through
and is pulled in gradually.

* **Hysteresis**: 0.15 st around each boundary between allowed notes, so a pitch sitting near a
  boundary doesn't flip back and forth.
* **Humanize**: after ~120 ms on one note, the time constant eases up by as much as
  400 ms × Humanize over the next 0.5 s. Short notes still snap.
* **Natural Vibrato**: the note centre is tracked with a 1.5 Hz two-pole filter, reset at every
  note change so steps never leak into it. The deviation from that centre is the vibrato, and
  `(gain − 1) × vibrato` is added after correction. At slow retune this scales the singer's
  vibrato exactly (±12 dB = ×4 / ×¼). At 0 ms it re-injects vibrato on top of the flat note.
  0 dB leaves the correction result untouched, so the default hard-tune stays flat.
* **Create Vibrato**: sine/square/saw LFO starting at note onset + delay, 100 ms fade-in.
  Square flips instantly (robotic).

**TD-PSOLA.** Pitch marks are placed on the largest positive peak of a 1.2 kHz low-passed copy
of the input, one period apart. Using the same polarity on the same smoothed peak keeps marks
phase-coherent from cycle to cycle, so re-placed grains add without flamming. Synthesis marks
advance by `T_in / ratio`, and each one copies the grain of the nearest analysis mark whose
right edge is already available. A ratio change therefore takes effect at the next grain:
there is no glide in the shifter.

**Character** (0 → 100) morphs the grain window from a 2-period Hann (COLA, smooth) to a
1.3-period Hann³ (short, peaky grains). Short peaky grains give a pitch-synchronous buzz, the
glassy edge. They also cost ~2–3 dB above 3 kHz, so a Character-linked high shelf (3.5 kHz,
up to +5 dB) more than compensates. Measured at unity ratio:

| Character | 0–1 kHz | 1–3 kHz | 3–6 kHz | 6–10 kHz | 10–16 kHz |
|---|---|---|---|---|---|
| 0 (clean) | 0.0 | +0.1 | 0.0 | −0.2 | −1.0 dB |
| 70 (default) | −0.7 | +0.4 | +0.7 | +1.5 | +0.9 dB |
| 100 (hard) | −1.0 | 0.0 | +1.0 | +2.2 | +1.6 dB |

A slow (80 ms) voiced-only level match keeps the shifted path at the input's RMS, because
peaky grains centred on glottal peaks otherwise read up to +3 dB hot.

### Formant / Throat: grain resampling (why not LPC)

Both were allowed by the spec, and LPC was the initial plan. During the synthesis design I
switched to **grain resampling**, because in TD-PSOLA it is the native mechanism:

* Pitch is set only by the *spacing* of grains, and the spectral envelope lives *inside* each
  grain. Reading a grain at speed 1/α stretches it by α and scales its envelope by 1/α, while
  the spacing (pitch) stays put. Formant correction ON is therefore free: plain PSOLA already
  preserves the envelope, and ON simply means α = Throat. OFF sets α = Throat / ratio (chipmunk).
* LPC would add an inverse filter, a re-synthesis filter per channel and an analysis frame.
  It would also need a way to *warp* an all-pole envelope by 50–180%, which means pole-angle
  scaling (root finding) or re-fitting from a warped spectrum. Both are expensive, and the
  first is numerically fragile. Grain resampling costs one interpolated read per sample and
  adds zero latency.
* Detail that matters: the output grain length is set by Character alone, and α only changes
  how much source a grain reads (2H/α). Without this, Throat above ~120% comb-filtered itself
  (stretched grains overlapped with mismatched periodicity), and Throat below 100% smeared the
  envelope.

Measured on a synthetic /a/, using the formant-envelope centroid from `harmonicEnvelopeCentroid`
in the tests:

* Formant correction holds the envelope within 5% at ±12 st.
* With formant correction off, the envelope follows pitch (×1.3 at +12 st, ×0.69 at −12 st).
* Throat 150% gives ×0.73 (ideal 0.71). Throat 70% gives ×1.15 (ideal 1.28). Brightening
  reaches roughly 60% of the ideal in log terms, darkening almost all of it.

### Measured end to end (tests + harness)

* Correction accuracy: output f0 within 0.1–0.5 ct of the target note across 84–450 Hz, with
  and without formant correction and at ±12/+7 st transposes. Level within ±2 dB.
* 0 ms retune on a C3→G3 glide: >85% of frames sit within 8 ct of a semitone, <5% are between
  notes (the step frames). At 250 ms retune >30% of frames are between notes (glide kept).
  Plot: `docs/phase3/glide_retune0.png` vs `glide_retune150.png`.
* Unvoiced noise passes through unshifted (residual < −30 dB) even with +12 st transpose.
* CPU (this container, Release, stereo, 128-sample blocks): 2.1% @ 44.1k, 2.4% @ 48k,
  2.6% @ 96k of one core.

---

## Latency

Reported latency = **9.0 ms** (397 samples at 44.1 kHz, 432 at 48 kHz). It is constant for
the session, so changing the Input Type never makes the host recompensate.

How the shifter lives with it: a grain needs its right half (about one period) available
before its left edge is output. When the period is long, the shifter copies an *older* pitch
mark's grain, one or two periods back. For a periodic voice that is the same waveform, so the
pitch and timbre are unaffected. The cost is only time smear at note onsets: about 1.5 periods
on a 55 Hz note (≈ 27 ms), and none above ~220 Hz. Agreed trade-off: low latency over bass
onset sharpness.

Pitch analysis adds no audio latency. It runs on the undelayed input, and each decision is
timestamped so it lands on the audio it was measured on.

## Parameters (all in APVTS, automatable, saved with state)

| ID | Range | Default | Notes |
|---|---|---|---|
| retuneSpeed | 0–400 ms (skewed, centre 50) | 0 ms | time constant; **not smoothed**: 0 ms = instant step |
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
| oversampling | 1x / 2x | 1x | character stage — *not wired yet (Phase 6)* |
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
