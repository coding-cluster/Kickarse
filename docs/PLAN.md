# Kickarse — Build Plan & Status

Lead: orchestrates agents, owns the contract (`docs/`, `src/shared/Params.h`, `Bridge.h`, `Envelope.h`, `src/dsp/Engine.h`).

## Phase 1 — foundations (parallel)
| Track | Agent | Status |
|-------|-------|--------|
| Design research, DESIGN.md, HTML prototype, fonts & generated assets | Opus design agent | **done** — approved; screenshots sent to user. Now on Phase 2 (NanoVG UI in `src/ui`, owns `plugin/KickarseUI.cpp`) |
| Bridge additions for UI (time sig, sc level, out peak, MIDI activity, ring scope) + `Envelope::evaluateLeft` | Opus DSP agent (follow-up) | **done** — 42/42 tests |
| DSP engine + Envelope model + tests | Opus DSP agent | **done** — 39/39 tests (lead re-ran), worst case spectral+multi 66× realtime; notes in `docs/DSP.md`; demo WAVs in `%LOCALAPPDATA%\KickarseBuild\dsp\renders` |
| Headless editor model (`src/editor`, tools/selection/snap/Quick Shift/undo/recording→envelope) | Opus editor agent | running (started early, Phase 2 item) |
| CMake, DPF glue (params/state/sidechain/MIDI/timepos), placeholder UI, build.ps1, VST3 validator, VST2/CLAP host test | Sonnet build agent | **done** — `build.ps1` → `dist\`; validator 46/47 (bypass-persistence = DPF united-controller trade-off of DIRECT_ACCESS, accepted); VST2 + CLAP host tests pass |
| Presets / shapes / favourites store + factory content | Sonnet preset agent | **done** — 36/36 tests (lead re-ran); 30 presets, 48 shapes |

### Status 2026-09-26 13:00 — release candidate built
- UI (Phase 2) done and wired; editor model 63/63; DSP 42/42; presets 36/36; VST2/CLAP host test PASS; VST3 validator 46/47 (known bypass-persistence item).
- `dist\Kickarse-Setup.exe` built from real payload; silent install to temp dir byte-identical, uninstall clean.
- Open: DAW testing by the user; preset "Save as" over an existing user name fails silently; Sync "One bar" view playhead assumes first cycle.

### (superseded) Status 2026-09-26 ~10:30 (agents paused by user after usage limit)
- `src/editor` library compiles; tests only partly written (TestOps.cpp misses `#include <algorithm>`; no undo/Quick Shift/capture/fuzz tests yet).
- `src/ui` (~7k lines) does not compile yet: `View.cpp` mid-edit (members not declared in `View.h`, `EditorGlue.cpp` missing). `plugin/KickarseUI.cpp` is still the placeholder; UI size still 1000×660.
- Real installer not built yet (only fake-payload test). No DAW testing yet. No user README yet.
| Win32 installer (`Kickarse-Setup.exe`) + PowerShell fallback | Sonnet installer agent | **done** — `installer\build-installer.ps1 -PayloadDir dist`; tested silent install/uninstall with fake payload; real UAC/HKLM path untested |

## Phase 2 — product
- Lead reviews the prototype (and shows it to the user as an artifact) → feedback round.
- Design agent implements the NanoVG UI in `src/ui/` (takes over `plugin/KickarseUI.cpp`).
- Opus UI-engineering agent: headless editor model (tools, selection, snap/grid/swing, Quick Shift, pencil fit, undo/redo, recording → envelope) with tests, used by the design agent's views.
- Sonnet preset agent: preset/shape store (`src/shared/Presets*`, `Shapes*`), favourites, factory content (Sidechain / Rhythmic / Simple shapes + full presets).

## Phase 3 — integration & QA
- Full build, VST3 validator, VST2/CLAP host tests, standalone UI screenshots at 1×/2×.
- DSP listening renders review, CPU check.
- Installer built from real `dist\` payload, silent-mode install test into a temp dir.
- User-facing README (install, DAW sidechain routing for Bitwig / FL Studio / Ableton).
