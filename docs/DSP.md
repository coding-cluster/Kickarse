# Kickarse — DSP

Framework-independent audio engine (`src/dsp/**`) and the shared envelope model
(`src/shared/Envelope.cpp`). No DPF includes; builds as `kickarse_core`.

## Engine notes

### File map

| File | Contents |
|------|----------|
| `shared/Envelope.cpp` | segment curve, evaluation, editing rules, serialisation, `fromSamples`, `PhaseMap` |
| `dsp/Engine.cpp` | `Engine::Impl`: parameter snapshot, per-chunk signal flow, Bridge publishing |
| `dsp/CycleClock.*` | timeline phase: host sync, free time, triggered loop / one-shot |
| `dsp/EnvelopeTable.*` | 4096-cell lookup tables with exact steps, lock-free triple-buffer hand-over |
| `dsp/Trigger.*` | sidechain HP/LP filter, transient detector |
| `dsp/Crossover.*` | Linkwitz–Riley LR2 / LR4 crossover (TPT SVF) |
| `dsp/SpectralDucker.*` | 24-band analysis, target curve, interaction-matrix solve, 24-bell cascade |
| `dsp/RingModulator.h/.cpp` | audio-rate sidechain modulator |
| `dsp/Svf.h`, `dsp/DspCommon.h` | TPT SVF, smoothers, sanitising, FTZ/DAZ guard, fast log2/exp2 |

### Signal flow

`process()` splits any host block into internal chunks of at most 256 samples (fixed scratch
buffers inside `Impl`), so every block size works, including 0 and sizes above `maxBlockSize`.
Parameters, envelope tables and the transport are sampled once per host block. Everything
else (smoothers, control ticks, triggers, MIDI) is per sample, so the output is bit-identical for
any block partitioning; a test checks this.

1. **Inputs.** Samples are sanitised: NaN and Inf become 0, and magnitudes are clamped to 1e4.
   Main is converted to Mid/Side. The sidechain is summed to mono and passed through the trigger
   filter (HP at `trig_lowcut`, then LP at `trig_highcut`, 12 dB/oct Butterworth TPT SVFs). The
   filter always runs, so toggling `trig_filter` never clicks. The result is the **detection
   signal**, which drives the audio trigger, the spectral analysis, the ring modulator, the `extWave`
   display, recording, and `trig_listen`. This lets the trigger filter isolate a kick from a full
   drum bus in every mode, and it is still "detection path only": it never touches the main signal.
2. **Clock and envelope.** Triggers (see below) go to `CycleClock`, which produces the timeline
   phase. `PhaseMap::toNode` then maps it to node space, the table lookup gives y per band, and a
   one-pole `smooth` filter follows.
3. **Gains.** Each sample gets a band gain (volume and ring modes) or a spectral depth scale.
4. **Processing.** The optional spectral EQ runs first, then the optional LR crossover with per-band
   gains and solo. Each stage produces both a *wet* signal and a phase-matched *reference* (see
   Delta).
5. **Output.** This stage applies the Mid/Side amounts, delta, output gain, listen and the bypass
   crossfade, then updates the waveform bins.

Every stage is linear in the main signal, with identical time-varying coefficients on both
channels. That makes processing M/S equivalent to processing L/R. The Mid/Side slider then reduces
to a per-component wet/dry amount: `amountMid = 1` for slider ≤ 0, falling linearly to 0 at +100,
and the mirror image for Side.

### Timing

- **Sync (tempo time).** `phase = frac(ppq / rateBeats)` in double precision, computed per sample
  from the host ppq at block start plus `n · bpm / (60 · fs)`. While the transport is stopped, or
  the host sends no timing, the internal ppq keeps advancing at the last known tempo from the last
  position. Pressing play snaps to the host position.
- **Sync (free time, `time_mode` = ms).** A free-running loop of `length_ms`. When the transport
  starts it is re-aligned to the song position, and after that it is continuous, so tempo automation
  never makes it jump.
- **Triggered (MIDI, Audio, and Spectral/Ring with `trig_source` Audio or MIDI).** A trigger
  restarts the cycle at timeline phase 0 on that exact sample. The cycle length is `rateBeats` at
  host tempo, or `length_ms`.
  - Loop keeps cycling until the next trigger. One-shot plays one cycle and then holds the value of
    the last node (the table end value). While it holds, `envelopeActive` is 0.
  - Before the first trigger the clock waits: y = 1 (unity), `envelopeActive = 0`, and the Bridge
    phase is 0.
  - In Sync mode, and for `trig_source` Sync or Continuous, `play_mode` is ignored.
- **`trig_source` = Continuous** means y = 0 constantly, i.e. the full effect. The clock still runs
  in sync, but only for the display. With `spec_target` = Volume, Continuous disables the volume
  stage, because a constant full duck would be silence.
- **PhaseMap.** `rotate01 = rotate / 360` and `divisions = cycleBeats / gridBeats`, where
  cycleBeats is the rate in beats (sync) or 1 (free time), and `swing = swing / 100`.
  - Swing is a piecewise-linear warp over each complete pair of grid cells: the pair midpoint moves
    to 50 % + 25 % · swing. A trailing incomplete pair (odd or fractional `divisions`) stays straight
    so the warp remains a bijection.
  - The map is computed in double; `toNode(toTimeline(q))` returns q to within 2e-6 (tested over
    20 000 random configurations).

### Triggers

- **Audio.**
  - Peak follower with instant attack and 60 ms release, then a threshold with 3 dB hysteresis,
    then the `retrig_ms` hold-off.
  - The 60 ms release keeps the follower inside the hysteresis band between the half-cycles of a
    30 Hz kick body, so a single kick cannot re-arm the detector.
  - An event that crosses the threshold during the hold-off is consumed, never delayed.
  - Detection is per sample, so the trigger lands on the sample where the follower crosses the
    threshold. Measured on synthetic kicks with click, hats and pitch sweep: worst error 0.02 ms at
    44.1, 48 and 96 kHz, with no double triggers. The optional 150 Hz low-pass of the trigger filter
    adds about 1.6 ms of group delay, which is inherent to filtering.
- **MIDI.**
  - Note-on with velocity > 0, filtered by `midi_note` (−1 means any note). Positions are
    sample-accurate; frames beyond the block are clamped to its last sample.
  - Velocity factor = `1 − velocity% · (1 − vel/127)`. It is latched at the trigger and multiplies
    `depth` everywhere: volume, spectral scale and ring amount.

### Envelope playback

`setEnvelope(band, env)` normalises a copy of the envelope, renders it into a table and publishes
it lock-free.

- **Table layout.** Each table holds 4096 cells with linear interpolation, the cycle-end value, and
  a list of vertical steps. A cell that contains a step interpolates up to the step's left value
  and restarts from its right value, so steps are exactly vertical at any cycle length. Plain
  interpolation would turn a step into a one-cell ramp, which is up to 4 ms on a 4/1 cycle.
- **Hand-over.** Tables move between threads through a triple buffer: the writer swaps into a shared
  "middle" slot with a dirty bit, and the audio thread swaps that slot into its "front" slot at
  block start. All three tables per band are allocated in the constructor, so nothing is allocated
  or freed while running. A mutex serialises writers; the audio thread never touches it.
- **`smooth`.** A one-pole filter on y with time constant `20 ms · (smooth/100)²`. At 0 there is no
  smoothing.
- **Gain.** `g = 1 − depth · velocity · (1 − y)`. Depth, the mixes, output gain and the M/S amounts
  are smoothed with a 15 ms one-pole. Bypass is a 10 ms linear crossfade to the exact dry input,
  bit-exact once the fade completes.

### Multiband

- **Crossover.** Linkwitz–Riley from TPT SVFs, zero latency, minimum phase.
  - LR2 is one Q = 0.5 SVF with `low = LP` and `high = −HP` (inverted polarity); the sum is a
    first-order allpass.
  - LR4 is a Butterworth SVF whose LP and HP outputs each pass through a second Butterworth SVF;
    the sum is a second-order allpass.
  - Measured `|low + high|` deviation from 20 Hz to 20 kHz is 0.0002 dB for crossovers at 40 Hz to
    15 kHz.
  - The crossover frequency is smoothed in log frequency (20 ms), with coefficients updated every
    16 samples.
- **Band routing.** The low band uses envelope A and `lo_mix`; the high band uses envelope B (or A
  when `env_link` is on) and `hi_mix`. `band_solo` outputs one band, processed. With multiband off,
  only envelope A and `depth` apply and `lo_mix` is ignored.

### Delta and reference

`delta` outputs `reference − wet`, where the reference is the main signal passed through exactly
the allpass path the wet signal took. Without multiband the reference is the input itself, so delta
is exactly `dry − wet` (tested to 2e-6). With multiband it is the crossover sum. Using the
phase-matched reference means that with nothing removed, delta is silent rather than an allpass
residue. The Mid/Side amounts use the same reference, so partial processing never combs.
`out_gain` applies after delta. `trig_listen` replaces the output with the detection signal and
bypasses `out_gain`. Bypass still crossfades to the dry input.

### Spectral mode

**Analysis.** There are 24 log-spaced bands from 30 Hz to 16 kHz, 0.394 octave apart. Their
centres are written to `Bridge::specCenterHz` in `prepare()`, capped at 0.45·fs.

- The **sidechain** runs through 4th-order band-passes: two cascaded SVFs with Q = 2.34 each, so
  neighbouring bands cross at −3 dB, 2 bands away is at −25 dB and 4 bands away at −39 dB. Each band
  has a peak follower using `spec_attack` / `spec_release`.
- The **main** signal (Mid) gets the same band-pass bank with a fixed 10 ms / 250 ms follower.
- Both banks are SSE-vectorised, four bands per register.

**Target (per band, every ~0.33 ms control tick).**

- *Drive.* `cut = 10·log10(1 + (S/T)²)`, capped at `spec_range`. This is a soft-knee 1:1 curve: each
  dB of sidechain band level above the threshold T adds a dB of cut, so the cut follows the kick's
  decay dB for dB. `spec_sens` sets the threshold: T = −6 dBFS at 0 %, −60 dBFS at 100 %
  (−38 dBFS at the default 60 %).
- *Presence.* How much the main signal occupies the band: a smoothstep over the band's level from
  −30 to −6 dB relative to the main's loudest band, times an absolute gate from −80 to −60 dBFS.
  The cut is multiplied by presence, so bands the main signal doesn't occupy are never cut. Such a
  cut would only add phase shift and coloration without making room. This is what makes the EQ
  target only the frequencies that actually compete.
- *Envelope.*
  - `spec_target` = Depth: the per-band scale is `depth · velocity · mix · (1 − y)`.
  - `spec_target` = Volume: the spectral scale is `depth · velocity · mix`, and the envelope then
    performs normal volume ducking after the EQ.
- *Multiband.* Each spectral band blends the low-band scale (envelope A, `lo_mix`) and the
  high-band scale (envelope B, `hi_mix`). The weights are the Linkwitz–Riley magnitude responses at
  the band centre: `wLow = 1/(1 + (f/fc)^n)` with n = 2 (LR2) or 4 (LR4), and `wHigh = 1 − wLow`
  (LR magnitudes sum to exactly 1).
  - I chose to weight the EQ bands rather than run a second EQ per crossover band. This keeps one
    cascade (half the CPU), avoids splitting the signal before a nonlinear-looking dynamic EQ, and
    makes the transition around the crossover follow the crossover's own slope, with no hard switch
    where a spectral band straddles fc.
  - The crossover itself is still in the path when multiband is on, for `band_solo` and for the
    Volume-target duck. A second crossover instance supplies the phase-matched reference.

**Equaliser.** The EQ is a cascade of 24 TPT-SVF peaking filters (RBJ-style, constant midpoint-gain
bandwidth, Q = 2.4) at the analysis centres. It is zero-latency and minimum-phase, and cheap to
modulate.

- *Interaction matrix.* Cascaded bells interact strongly: 24 bells each set to −6 dB sum to about
  −12 dB. The bell gains are therefore solved from the targets through an interaction matrix. This
  is the "accurate cascade graphic equaliser" approach of Välimäki & Liski, 2017. At `prepare()`
  the dB response of each bell (at −18 dB, evaluated at the centres and the midpoints between them)
  forms B. The regularised least-squares solve `(BᵀB + 0.05·I)⁻¹ Bᵀ E` then gives a fixed 24×24
  matrix M, and per tick `bellGains = M · targets`, clamped to −72…+12 dB.
- *Tuning.* The parameters were tuned offline over 300 kick-like target curves (3–48 dB). The mean
  error is 3.3 % of the cut depth (maximum 8.5 %), and there is at most 0.8 dB of overshoot between
  bands. A test measures −17.8 dB at 55 Hz for an 18 dB target, with 0.00 dB change at 6 kHz.
- *Coefficient ramps.* Bell coefficients (`a1` and the band-pass mix `m1`) are updated every 16
  samples at 44.1/48 kHz (32 at 88.2/96 kHz, 64 at 192 kHz). They ramp linearly per sample, so
  there is no zipper noise. `a2 = g·a1` and `a3 = g·a2` are recomputed per sample, so the filter
  structure stays a valid SVF throughout the ramp.
- *Wavefront.* The cascade is serial and latency-bound, so it runs as an SSE "wavefront": four
  consecutive bells share one register, each lane one sample behind the previous. That advances four
  bells per step. Pipeline fill and drain are masked at chunk edges, so the result is still
  zero-latency and bit-identical to the plain loop, with about 5× less CPU.

### Ring mod mode

The modulator is `m = e / P ∈ [0,1]`:

- `e` is an attack/release follower of |detection signal| using `ring_attack` / `ring_release`.
- `P` is the peak of `e`, with instant attack and a 2 s release, floored at −48 dBFS. The floor
  stops sidechain noise from being normalised up to full modulation.

The output is `main · (1 − depth · velocity · mix · (1 − y) · m)`. In multiband mode this applies
per crossover band.

Why this form:

- **It carves the waveform.** With the default fast settings (0.1 ms / 5 ms), `e` is essentially
  the rectified kick waveform. The bass is multiplied by `(1 − |kick|/P)`, so it is removed exactly
  where the kick waveform is large and passes at the kick's zero crossings. This is amplitude
  modulation at the kick's own cycle rate: new sidebands, no slow pump.
- **It bounds the peak.** At full amount, if |bass| ≤ P then |kick + bass·(1 − |kick|/P)| ≤ P. The
  overlap can never exceed the kick's own peak. This is the "controls the peak build-up without
  pumping" behaviour: in the test, the kick + bass overlap peak falls from 1.457 to 0.887, which is
  the kick's own peak.
- **Why not a signed modulator.** A signed ring modulator, main · (1 − sc/P), was rejected. It
  raises the gain above 1 on the negative half-cycles, which makes overlap peaks worse, and it
  sounds like a ring mod rather than a ducker.
- **Slower settings** (for example 5 ms / 150 ms) blur `m` into a fast, level-normalised ducker with
  fewer sidebands. The attack/release pair is the speed ↔ smoothness control.

### Bridge

Written once per block unless noted:

- `phase`, `valueA/B` (the smoothed y), `envelopeActive`, `bpm`, `hostPlaying` and `cycleSeconds`.
- `triggerCount`, incremented per trigger with `fetch_add`.
- `gainReductionDb`: the deepest gain in the block. In spectral mode it is the deepest band cut plus
  any volume stage, and it reads 0 when fully bypassed.
- **Waveforms**, per timeline bin. A bin is reset when the playhead enters it and holds the peak
  of max(|L|,|R|) (main and output) or |detection signal| (ext). Bins crossed within one sample
  (cycles shorter than 512 samples) are filled. Nothing is written while waiting or holding.
- **Spectral.** `specCutDb` holds the per-band *target* cut (what the curve should look like) and
  `specScDb` the sidechain follower level. Both reset to 0 / −120 when leaving spectral mode.
- **Time signature.** `timeSigNum` / `timeSigDen` are copied from `TransportInfo`, clamped to
  1–64; the plugin supplies them from DPF's `beatsPerBar` / `beatType` and defaults to 4/4.
- **`scLevelDb`.** The trigger detector's follower level in dBFS (instant attack, 60 ms release, on
  the detection signal after the trigger filter). This is exactly what `threshold` is compared
  with, so the UI can draw it as a threshold meter. A steady −12 dBFS tone reads −12.0 to −12.8 dB.
- **`outPeakDb`.** The peak of the final output with a 300 ms exponential decay.
- **`lastNote`, `lastVelocity`, `noteCount`.** Updated on every MIDI note-on in any mode, before
  the `midi_note` filter, so they work for an activity LED and MIDI Learn. `triggerCount` counts
  only notes that actually trigger.
- **`ringModWave`.** Ring mode only: per timeline bin, the peak of the effective modulation amount
  `depth·velocity·mix·(1−y)·m`, i.e. 1 − gain, with the maximum of the two bands in multiband.
  Other modes leave it untouched.
- **Recording handshake,** exactly as documented in `Bridge.h`.
  - Armed becomes Recording at the next timeline wrap (Sync source) or the next trigger (Audio/MIDI)
    via compare-exchange, so a UI cancel is never overwritten.
  - `recBuf` gets the peak |detection signal| per timeline bin over exactly one cycle, and
    `recProgress` is updated as it fills.
  - Done is stored with release semantics after the last write.
  - A cycle cut short by a retrigger leaves its unvisited bins at 0.
  - A cancel (Idle) is seen at the next bin boundary or block, after which writing stops.

### Real-time safety

- `process()` does not allocate, lock, perform I/O or throw.
- It sets FTZ/DAZ for its duration and restores the caller's MXCSR afterwards.
- The audio thread only touches atomics (relaxed, apart from the handshake), its own table slot
  and preallocated state.
- Inputs are sanitised, so NaN and Inf cannot enter any filter. This is tested with NaN, Inf and
  1e30 injections followed by 30 s of silence: no non-finite and no subnormal output.
- All time constants are derived from fs, and tests cover 44.1 to 192 kHz.

### Performance

Release build, one core, 48 kHz stereo, 512-sample blocks, from the test runner on the dev
machine:

| Mode | × realtime |
|------|-----------:|
| Sync / MIDI / Audio | 266 / 285 / 294 |
| Sync + multiband | 201 |
| Spectral (audio trigger) | 82 |
| Spectral, continuous | 88 |
| Spectral + multiband + volume target (worst case) | **68** |
| Ring mod / ring mod + multiband | 439 / 188 |
| Delta | 326 |

### Tests

`tests/` builds a self-contained runner `kickarse_tests`: 39 tests with about 327k checks, the
benchmarks, and the WAV renders.

- **Standalone:** configure with
  `cmake -S tests -B %LOCALAPPDATA%\KickarseBuild\dsp -G "Visual Studio 17 2022" -A x64`, build with
  `cmake --build … --config Release`, then run `…\Release\kickarse_tests.exe [name-filter]`.
- **Top-level:** the root CMakeLists adds `tests/` after `src/`, and the guard reuses the existing
  `kickarse_core`.
- **Renders:** the runner writes kick + bass demos through every mode to
  `%LOCALAPPDATA%\KickarseBuild\dsp\renders\*.wav` (32-bit float, 48 kHz).

### Notes for the UI and plugin glue

- **`setEnvelope`** may be called from any non-RT thread, even concurrently: writers are serialised
  internally. It renders about 4096 points (tens of µs) and never blocks the audio thread. The last
  call wins.
- **Envelope serialisation (`KE1`).** The format is `"KE1 x,y,t,f;x,y,t,f;…"`:
  - Floats are shortest round-trip `std::to_chars`, so the format is locale-independent and
    round-trips exactly. `f` is the flag bits as an unsigned decimal.
  - Parsing accepts whitespace around tokens, a trailing `;`, and a missing `f`.
  - Parsing rejects a wrong or unknown version tag, non-finite numbers, fewer than 2 or more than
    128 nodes, trailing garbage, and inputs over 64 KB. On rejection it returns false and leaves the
    envelope untouched.
  - Accepted values are clamped and normalised.
- **Envelope semantics.**
  - Tension > 0 holds the segment's start value longer (ease-in); tension < 0 leaves it quickly.
    The curve is `(e^{6t·u} − 1)/(e^{6t} − 1)`, so |t| = 1 reaches 4.7 % at mid-segment.
  - `evaluate()` is right-continuous at steps and wraps. `evaluateLeft()` is for drawing: it is
    left-continuous (the value *before* a step), clamps the phase to [0,1] instead of wrapping,
    and returns the last node's value at 1.
  - `insert()` places a node after any nodes with the same x, and never before the first or after
    the last node.
  - The default shape is `0 → 1` with tension 0.3, recovering at 50 %. The default sets no Quick
    Shift flags; the UI chooses the group.
- **`fromSamples(y, n, tol)`.** Sample i sits at x = i/n (the `render()` convention). The fit is a
  curve-aware Ramer–Douglas–Peucker: a span is accepted when one tensioned segment fits within `tol`
  (minimax tension from a scan plus golden-section search), and otherwise split at the maximum chord
  deviation. The tolerance grows ×1.5 until the result fits 128 nodes. The default duck refits into
  its 3 nodes with zero error, and an exponential recovery into 4 nodes at a tolerance of 0.003.
- **Recording contour.** `recBuf` holds raw per-bin peaks of the waveform, so it ripples at the
  kick's frequency: a 0.5 ms bin catches only part of a 50 Hz cycle. Before inverting (`y = 1 −
  peak/max`) and calling `fromSamples`, the UI should apply a peak-hold with release (for example a
  running max with about 5–10 ms decay).
- **Recording and timeline phase.** Recording and the waveform bins are indexed by *timeline*
  phase. To store a recorded shape in node space when rotate or swing is non-zero, map each bin
  through `PhaseMap::toNode`.
- **One-shot hold.** One-shot holds the last node's value even when rotate ≠ 0, as specified. With
  rotation this may differ from the value displayed at the right edge; `smooth` covers the
  transition.
- **Buffers and events.** `process()` accepts null `mainIn`/`scIn` pointer arrays (treated as
  silence) and individual null channels (a missing left channel is silence, a missing right
  channel mirrors the left), plus a null `notes` array.
