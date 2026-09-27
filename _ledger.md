# Kickarse ledger

Append-only record of work done in this repository (newest last). Read it before starting a task.

## 09/26/2026 — UI fixes from early testing (commit 20fc94a)

- Changed: plugin/KickarseUI.cpp (re-assert saved UI size after open, keyboard focus for text
  entry), src/ui/View.* (idle split into interaction vs ambient ≤30 fps, text-entry click/keys,
  "Name taken" on save), src/ui/Theme.h (textDim/textMute lifted to ≥4.5:1), src/ui/Displays.cpp
  (bigger GR meter), docs.
- Verified: syntax-only compile against DPF headers (no Windows build available).
- Notes: liveMoved() compared fields that are not drawn when stopped in Sync mode; fixed in the
  next entry.

## 09/26/2026, 23:10 — Hover/idle CPU analysis + Smooth dotted curve

- Changed:
  - plugin/KickarseUI.cpp: requestRepaint() caps every repaint at 60/s, deferring to the 16 ms idle.
  - src/ui/View.cpp/.h: motion repaints only on drag / hot or hint change / tooltip /
    repaintsOnMove(); envelopeAtRest() (stopped + Sync): liveMoved ignores phase/values, GR = 0
    unless output > −90 dBFS.
  - src/ui/Widget.h, EditorView.h: Widget::repaintsOnMove() (editor = false, it self-reports).
  - src/ui/Library.*: thumbnails sampled once per refresh; Displays.*: crossover curve cached per
    (fc, slope), BarView evaluates once; EditorView.cpp drawWaves 2:1 peak decimation.
  - src/shared/Params.h + src/dsp/Engine.cpp: envSmoothMs() shared (bit-identical math).
  - src/ui/EditorView.cpp: drawSmoothed() dotted curve = DSP one-pole over the drawn curve.
  - docs/design/DESIGN.md §7.7, §13.4.
- Reused: existing EditorController hover-change flag, Gfx::dashedPolyline, onePole formula.
- Verified (Linux, Release): kickarse_tests 42/42, kickarse_editor_tests 63/63,
  kickarse_preset_tests 36/36, in-plugin self-test 28/28 PASS via a scratch DGL harness
  (Xvfb + Mesa). Harness paints/s: stopped idle 30 → 0.3; hovering 1 per mouse event → 13
  (stopped) / 31.5 (playing, capped). callgrind View::paint 5.78 M → 5.09 M instr/paint.
  Snapshot at Smooth 100 %: engine output waveform follows the dotted curve.
- Notes: DSP is 0.2–1.1 % of a core in every mode; Bitwig's DSP graph is engine-wide, so UI-thread
  load in the plugin host shows there. pugl on Windows uses swap interval 1 (vsync), which some
  drivers busy-wait. Remaining per-frame cost is NanoVG tessellation + text (static widgets redrawn
  every frame); next step would be caching static layers in an FBO.

## 09/27/2026 — Text entry: caret, selection, word navigation

- Changed: src/ui/View.h/.cpp — Entry keeps caret/anchor (UTF-8 byte offsets) instead of a
  select-all flag; entryKey() handles Left/Right (Ctrl = word, Shift = extend), Home/End,
  Backspace/Delete (Ctrl = word), Ctrl+A; typing replaces the selection; click places the caret,
  Shift+click extends, drag selects, double-click selects all; blink restarts on each edit.
  drawEntry() records caret stops (x per code point) for drawing and mouse hits.
  docs/design/DESIGN.md §6.11.
- Reused: existing entry plumbing (open/commit/cancel, box hit test); stdlib only.
- Verified: harness entry test 19/19 (real View::keyboard/character/mouse/motion), UI self-test
  28/28, DSP 42/42, editor 63/63, presets 36/36; View.cpp -Wall -Wextra clean (2 pre-existing
  sign-compare warnings). Screenshot: selection and caret land on glyph boundaries.
- Notes: Shift+letters already worked (pugl turns WM_CHAR into text); the missing piece was a
  caret. Commit trims leading/trailing spaces (pre-existing). No clipboard yet (Ctrl+C/V/X);
  the preset browser's search field still only appends.

## 09/27/2026 — Typing did nothing in the name field (host keyboard delivery)

- Changed: plugin/KickarseUI.cpp — while an entry is open, a per-thread WH_GETMESSAGE hook
  handles WM_KEYDOWN (TranslateMessage + mapped DGL key) and WM_CHAR for the plugin window and
  turns them into WM_NULL; winReleaseKeyboard(); debugLog() for focus/hook/key events.
  src/ui/View.* — WindowHost::winReleaseKeyboard, called on commit (before the callback) and
  cancel; Ctrl+A ignores AltGr. src/ui/PresetBrowser.cpp — no search caret while a text entry is
  open. docs/design/DESIGN.md §6.11.
- Reused: JUCE's approach for plugin keyboard input on Windows (message hook), DGL key codes.
- Verified: MinGW cross-build of Kickarse-vst3 (links; hook APIs imported; no warnings in our
  files); harness entry test 19/19, UI self-test 28/28; screenshot of Save as… with the browser.
  NOT verified in a host: no Windows/DAW here.
- Notes: cause not proven on the user's machine; if typing still fails, KICKARSE_UI_DEBUGLOG
  shows whether keys reach the hook ("hook key/char") or DPF ("dpf key/char").
