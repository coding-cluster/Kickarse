# Kickarse — Product Spec

Kickarse is a sidechain / ducking plugin (VST2, VST3, CLAP) for Windows, for use in Bitwig, FL Studio
and Ableton Live. It is an original product: it must not reuse the name, logo, branding, artwork or
trade dress of any existing commercial plugin. Feature parity with best-in-class envelope-based
sidechain tools is the goal; the look is our own.

## 1. Core concept

A drawable **envelope** (gain curve over one cycle) is applied to the main input. The cycle is either
locked to the host tempo or (re)started by a trigger. `y = 1` is unity gain, `y = 0` is full duck.
Effective gain = `1 - depth * (1 - y)`.

## 2. Modes (parameter `mode`)

| Mode      | What drives the envelope                                   | What the envelope does                                            |
|-----------|------------------------------------------------------------|-------------------------------------------------------------------|
| Sync      | Host transport position (PPQ) at the chosen rate           | Volume ducking                                                    |
| MIDI      | Note-on from the plugin's MIDI input (optional note filter, velocity sensitivity) | Volume ducking                             |
| Audio     | Transients in the sidechain input (threshold, hold-off, trigger filter) | Volume ducking                                       |
| Spectral  | `trigSource`: Audio / Sync / MIDI / Continuous             | Scales spectral ducking depth over time, **or** (spec target = Volume) ducks volume after the spectral stage |
| Ring Mod  | `trigSource`: Audio / Sync / MIDI / Continuous             | Scales ring-mod depth over time                                   |

### 2.1 Spectral ducking
- Analyse the sidechain with a 24-band filterbank (log-spaced, ~30 Hz – 16 kHz).
- Per band, follow the sidechain level with independent **spectral attack / release**.
- Build a **reversed EQ curve** on the main signal: cut the bands where the sidechain has energy,
  i.e. only the frequencies that compete. Max cut per band = `specRange` dB, scaled by `specSens`.
- The envelope shapes the spectral depth over time (soft & transparent ↔ pronounced pump).
- Alternative routing (`specTarget = Volume`): spectral stage runs at full depth and the envelope
  performs conventional volume ducking after it.
- Zero latency (IIR filterbank / dynamic peaking EQ — no FFT look-ahead).

### 2.2 Sidechain ring modulation
- Multiply the main signal by a modulator derived from the sidechain waveform, so the kick "carves"
  the bass at audio rate instead of a slow volume dip. Creates new spectral content rather than
  selectively cutting.
- Very fast response for overlapping kick + bass; controls the peak build-up from their overlap
  without conventional pumping.
- `ringAttack` / `ringRelease` smooth the modulator (speed ↔ smoothness / fewer artefacts).

## 3. Multiband (MULTI)
- Split into low / high bands at an adjustable crossover (20 Hz – 20 kHz).
- Zero-latency, minimum-phase Linkwitz–Riley crossover, 12 or 24 dB/oct.
- Each band has its own envelope (`envA` = low/main, `envB` = high) and intensity (Lo Mix / Hi Mix).
- **Link** makes the high band follow the low envelope (they move as one).
- Individual band solo. Works with Spectral and Ring Mod modes.

## 4. Envelope editor
- Nodes with per-segment curvature (tension). Endpoints fixed at x = 0 and x = 1. Vertical jumps
  allowed (two nodes at the same x) for step shapes.
- Tools: **Select/Edit** (drag nodes, drag segment to bend, double-click add/delete, marquee
  select, multi-drag), **Line** (draw straight segments), **Pencil** (freehand draw → simplified nodes).
- **Grid** (note values), **Snap** toggle, **Swing** (moves every second grid cell, playback is
  warped accordingly — MPC-style 50 %→75 %).
- **Smooth** amount (removes clicks from sharp edges).
- **Rotate** (phase offset of the whole cycle, 0–360°).
- **Quick Shift**: a bar that moves a group of nodes (the critical dip + recovery) horizontally in
  one move. The user can edit the Quick Shift range and choose which nodes are grouped; the group
  then moves together.
- **Waveform display** behind the curve: MAIN input and EXT (sidechain) waveforms, each toggleable.
- **Audio → envelope**: record the incoming sidechain signal; its contour becomes an editable shape
  (inverted so a kick produces a duck), which can be refined, saved and reused.
- **Undo / Redo** for all edits made in the UI.

## 5. Timing
- Rate: tempo-synced note values (4/1 … 1/64 incl. dotted & triplets) or free time in ms.
- **One-shot** or **Loop** playback (one-shot plays the cycle once per trigger then holds the last value).

## 6. Monitoring & routing
- **Delta monitoring**: hear only what is removed (dry − wet).
- **Mid/Side**: slider from Mid-only through Stereo to Side-only processing.
- **Sidechain trigger filter**: HP/LP on the detection path only (e.g. isolate the kick from a full
  drum bus), with a listen switch.
- Band solo (multiband).

## 7. Presets
- **Full presets** (all parameters + envelopes) with categories, prev/next, save, dirty marker (`*`),
  **Favourites**.
- **Shape library** (envelopes only) with categories *Sidechain*, *Rhythmic*, *Simple*, *User*,
  shown as thumbnails; user shapes can be saved (including recorded ones).
- Factory content compiled into the binary; user content in `Documents\Kickarse\`.

## 8. Delivery
- VST3 (`Kickarse.vst3`), VST2 (`Kickarse.dll`), CLAP (`Kickarse.clap`), 64-bit Windows.
- A single installer `.exe` letting the user choose formats and the VST2 folder, plus an uninstaller.
- VST2 is built with DPF's clean-room VST2 header (no Steinberg VST2 SDK needed).
