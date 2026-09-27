# Kickarse — Architecture & Team Contract

Read `docs/SPEC.md` first. This file says **where things live, who owns them, and the rules that
keep parallel work from colliding**.

## Stack
- **DPF** (DISTRHO Plugin Framework, ISC) in `external/DPF` — builds VST2 (clean-room header, no
  Steinberg SDK), VST3 (DPF's own "travesty" API), CLAP, and a standalone `jack` target (native
  audio fallback on Windows) which is handy for looking at the UI without a DAW.
- UI: DPF's DGL with **NanoVG on OpenGL** (`UI_TYPE opengl`). Vector drawing, gradients, images
  (PNG from memory), TTF fonts from memory.
- C++17, MSVC 14.44 (VS Build Tools 2022), CMake ≥ 3.22, static CRT (`/MT`) so the plugin needs no
  VC++ redistributable.
- DPF config: `DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1` (UI reads `Engine::bridge()` directly),
  `WANT_STATE 1`, `WANT_FULL_STATE 1`, `WANT_TIMEPOS 1`, `WANT_MIDI_INPUT 1`, 4 audio inputs
  (2 main + 2 sidechain with `kAudioPortIsSidechain`), 2 outputs, zero latency.

## Layout & ownership

| Path | Owner | Notes |
|------|-------|-------|
| `docs/SPEC.md`, `docs/ARCHITECTURE.md` | lead | product + contract |
| `src/shared/Params.h`, `src/shared/Bridge.h` | lead (contract) | additive changes only, report them |
| `src/shared/Envelope.h` | lead (contract) | DSP agent implements `Envelope.cpp`; additive changes only |
| `src/dsp/Engine.h` | lead (contract) | public API fixed; all state in `Engine::Impl` |
| `src/dsp/**`, `src/shared/Envelope.cpp`, `src/CMakeLists.txt`, `tests/**` | DSP agent | framework-independent, no DPF includes |
| `src/shared/Presets*`, `src/shared/Shapes*`, `resources/presets/**` | preset agent | file I/O, factory content, favourites |
| `docs/design/**`, `design/**`, `resources/fonts/**`, `resources/images/**`, `tools/assets/**` | design agent | research, design spec, prototype, generated assets |
| `src/ui/**` | design agent (+ UI engineers it is paired with) | NanoVG widgets, layout, editor view |
| `plugin/**`, top-level `CMakeLists.txt`, `build.ps1` | build agent | DPF glue: DistrhoPluginInfo.h, KickarsePlugin.cpp, KickarseUI.cpp |
| `installer/**` | installer agent | Win32 installer/uninstaller exe + PowerShell fallback |
| `external/DPF` | nobody | vendored, do not modify |

**Rule:** only edit files you own. If you need a change in someone else's file, make the smallest
additive change possible *only if it is required to compile*, and list it in your final report.

## Threads & data flow

```
 host ── params ──► KickarsePlugin::setParameterValue ──► Engine::setParameter (atomics, any thread)
 host ── state  ──► KickarsePlugin::setState("envA"...) ──► Envelope::deserialize ──► Engine::setEnvelope (lock-free publish)
 host ── audio ───► KickarsePlugin::run ──► Engine::process (audio thread: no alloc, no locks, no I/O)
                                               │
                                               ▼ writes
                                          Bridge (atomics)  ◄── reads every uiIdle() ── KickarseUI / src/ui
 UI edits ──► UI::setParameterValue / UI::setState (DPF routes to host + plugin)
```

- The audio thread never allocates, locks, logs, or touches files. Envelope changes arrive as
  pre-rendered lookup tables swapped via an atomic index; retired tables are freed on the
  non-RT thread.
- `KickarsePlugin` keeps the last state strings (for `getState`) behind a mutex used only by
  non-RT threads.
- The UI polls the Bridge in `uiIdle()` (~60 Hz) and repaints only what changed: interaction
  at once, live displays at most 30 times a second and only when they moved (DESIGN.md §13.4).

## State keys
`envA`, `envB` (Envelope::serialize), `uiState` (opaque, UI-owned), `presetName`.

## User data
`%USERPROFILE%\Documents\Kickarse\` → `Presets\<Category>\*.kkpreset`, `Shapes\*.kkshape`,
`favourites.txt`. Factory presets/shapes are compiled into the binary.

## Build
Build directories live **outside OneDrive** (OneDrive sync locks .obj/.pdb files):

```
cmake -S . -B %LOCALAPPDATA%\KickarseBuild\<your-dir> -G "Visual Studio 17 2022" -A x64
cmake --build %LOCALAPPDATA%\KickarseBuild\<your-dir> --config Release -- /m
```

Use your own `<your-dir>` (e.g. `dsp`, `main`, `ui`, `installer`) so parallel builds never share a
tree. Outputs: `<build>\bin\Kickarse.vst3\`, `Kickarse-vst2.dll`, `Kickarse.clap`, `Kickarse.exe`.

## Coding conventions
- Namespace `kick`. 4-space indent. `PascalCase` types/files, `camelCase` functions/vars,
  `kConstant` constants, trailing `_` for private members.
- `#pragma once`. No exceptions across the plugin boundary; no RTTI requirements.
- Comments explain *why*, not *what*. No dead code, no commented-out blocks.
- Denormals: flush-to-zero in `process` (set FTZ/DAZ via `_mm_setcsr`) and keep filters stable.
