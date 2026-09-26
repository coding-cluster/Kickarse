# Kickarse — Design system & UI specification

Single source of truth for the Phase 2 NanoVG UI (`src/ui/`). It goes with:

* `design/prototype/index.html`: the **executable spec**. It is a Canvas 2D implementation
  that uses only NanoVG-equivalent calls. When this document and the prototype disagree on a
  pixel, the prototype wins. Every number below is copied from it.
* `design/prototype/screenshots/*.png`: reference renders at 1× and 2×.
* `docs/design/RESEARCH.md`: the reasoning behind the choices.
* `tools/assets/*.py`: the scripts that regenerate every font and raster (§14).

All geometry is in **logical pixels at 100 % UI scale** (1×). Colours are sRGB hex. Angles are in
radians in a y-down space, measured clockwise from +x, which matches both Canvas and NanoVG.

---

## 1. Concept and personality

**"A precision instrument with one rude word on it."**

Kickarse ducks audio. Sidechain pumping is a club gesture, but getting it *right* (a duck that
recovers exactly before the next kick, a bass that gets out of the way only where it collides)
is careful work. The UI is therefore a calm, exact, warm-graphite instrument, and the cheek is
confined to places that cost nothing in clarity:

* **The wordmark** (§4.4). In "kickarse" the letters of *arse* drop on the hit and recover one
  by one, so the word draws a sidechain envelope. The joke is typographic and quiet.
* **The duck colour.** The envelope, Depth and every "amount of ducking" mark are rubber-duck
  yellow, because the plugin *ducks*.
* **Microcopy** in the hint line and empty states (a plain voice, occasionally dry; never
  emoji, never exclamation marks).

The controls themselves are serious: no mascots, no novelty knobs, no jokes on labels.

**Design principles**

1. *Signal flow reads left to right.* When (Time, Trigger) → Shape (editor, library) → How much
   and where (Depth, Bands, Stereo, Output). The hint line runs along the bottom.
2. *The curve is the interface.* The editor is the largest, calmest area and edits happen
   directly on it.
3. *Colour is information.* Three hues, each with one meaning everywhere: **yellow = the duck**
   (envelope, depth, cut), **red = the sidechain** (kick, trigger, threshold, capture),
   **blue = the high band**. Everything else is warm neutral.
4. *One physical object.* Only the Depth knob is rendered as a real material (ceramic on
   gunmetal). Everything that moves or scales is vector.
5. *Every value is visible and typeable.* Every knob and field prints its value and accepts a
   typed number.
6. *Progressive disclosure.* You see only the active mode's controls. Band tabs appear only when
   there are two envelopes. The multiband section dims when Split is off.

---

## 2. Canvas, grid and regions

### 2.1 Canvas

* **Base size: 1080 × 660 at 100 %.** Aspect ratio 1.636 is fixed.
  * Wide enough for a 588 px envelope plot plus two control columns. The reference plugin's
    editor was about 650 px *including* labels.
  * 660 px fits a 768 px laptop screen with the host's window chrome.
  * 1.5× gives 1620 × 990, which fits 1080p with a DAW taskbar.
* **UI scale:** 100, 125, 150, 175, 200 % from the menu, plus continuous drag on the resize grip
  in 5 % steps, clamped to 100–200 %. Minimum = 100 %.
* **Spacing unit: 4 px.** Section padding 16, column content inset 16, control gap 4–8, row
  pitch 26–28 for control rows.

### 2.2 Region map (logical px)

| Region | Rect (x, y, w, h) | Notes |
|---|---|---|
| Header rail | 0, 0, 1080, 44 | darker rail, groove at y = 44 |
| Left column: Time & Trigger | 0, 44, 244, 588 | content x = 16, w = 212; groove at x = 244 |
| Centre: Shape | 244, 44, 636, 588 | content x = 256, w = 612 |
| Right column: Amount | 880, 44, 200, 588 | content x = 896, w = 168; groove at x = 880 |
| Footer rail (hint line) | 0, 632, 1080, 28 | groove at y = 632 |

**Header.** Wordmark: em size 21, left edge at x = 16, baseline y = 27. Preset bar well at
(256, 8, 460, 28) containing:
* browse icon button (258, 10, 30, 24), divider at x = 290.5
* prev (292, 10, 24, 24)
* name hit-area (318, 8, 300, 28), text centred at x = 468
* next (620, 10, 24, 24), divider at x = 646.5
* favourite star (648, 10, 32, 24)
* save (682, 10, 32, 24)

"Saved" confirmation text at x = 726. Undo (736, 10, 28, 24), redo (766, 10, 28, 24). Settings
"⋯" (986, 10, 28, 24). Bypass key (1020, 8, 48, 28): lamp at (1032, 22), power glyph at
(1052, 22).

**Left column** (x = 16, w = 212).

| Element | Geometry |
|---|---|
| "Time" title | centre-y 64 |
| Note/ms segmented | (140, 54, 88, 20) |
| Rate stepper | (16, 76, 212, 38) |
| Cycle caption | y = 126 |
| Loop/One-shot segmented | (16, 140, 212, 26) |
| Groove | y = 182 (x 12–232) |
| "Trigger" title | y = 200; trigger LED at (224, 200); "hits" caption right-aligned at x = 214 |
| Mode segmented | (16, 210, 212, 26) |
| Mode panel | origin (16, 248), max 212 × 376 (§8) |

**Centre column.**

| Element | Geometry |
|---|---|
| Toolbar row | y 52–80 (h 28) |
| Tools group well | (256, 52, 88, 28); three 28 px keys from x = 258 |
| Grid dropdown | (352, 52, 88, 28) |
| Snap lamp | (446, 52, 62, 28) |
| Swing field | (514, 52, 86, 28) |
| Smooth field | (606, 52, 96, 28) |
| "Show" caption | x = 712 |
| In / Side / Out lamp keys | from x = 744; each is label width + 28, with 4 px gaps |
| **Editor display well** | (256, 88, 612, 328) |
| Plot | (268, 100, 588, 272) |
| Axis-label strip | (268, 374, 588, 14); labels at centre-y 381 |
| Quick Shift lane | (268, 392, 588, 16) |
| Editor footer row | y 424–448 (h 24) |
| Band tabs well (Split only) | (256, 424, 150, 24); Low key at 258 (w 54), link at 314 (w 34), High key at 350 (w 54) |
| Quick shift lamp | x = 256, or 414 when the band tabs show; w 98 |
| Rotate field | Quick shift x + 104, w 96 |
| Capture key | (776, 424, 92, 24) |
| Library tabs row | y 464–486; underline 2 px at y = 484 |
| Paging and "Save shape" | right-aligned in the tabs row, ending at x = 868 |
| Shape bank well | (256, 494, 612, 128); 8 × 2 cells of 76.5 × 64 |

**Right column** (x = 896, w = 168).

| Element | Geometry |
|---|---|
| "Depth" title | y = 64 |
| Hero knob centre | (980, 150) |
| Scale labels "0" / "100" | (936, 202) / (1024, 202) |
| Depth value | centre (980, 222) |
| Groove | y = 244 |
| "Bands" title | y = 262 |
| Split lamp | (1000, 251, 64, 22) |
| Crossover graph | (896, 282, 168, 66) |
| Slope segmented | (896, 356, 92, 22) |
| Solo L | (996, 356, 33, 22) |
| Solo H | (1031, 356, 33, 22) |
| Low mix knob centre | (934, 412) |
| High mix knob centre | (1026, 412) |
| Groove | y = 470 |
| "Stereo" title | y = 488, value right-aligned at x = 1064 |
| M/S slider | (896, 500, 168, 36); track x 904–1056 at y = 512 |
| Groove | y = 548 |
| "Output" title | y = 566 |
| Delta lamp | (998, 556, 66, 22) |
| Gain knob centre | (918, 598); value text at (946, 592), "gain" at (946, 606) |
| GR meter | (998, 592, 66, 5); caption right-aligned at y = 607 |

**Footer.** Hint text at (16, centre-y 646.5), 10.5 px. UI-size button (994, 636, 60, 20),
text right-aligned at 1040 with a caret at 1047. Resize grip (1058, 636, 22, 24), glyph at
(1070, 650).

### 2.3 Hierarchy and squint test

Blur the 1× render with a σ ≈ 9 px Gaussian and convert it to greyscale. The order must be:

1. The Depth knob (the only light-valued object).
2. The envelope curve and editor (largest saturated line on the largest dark field).
3. The rate display and preset name.
4. Everything else, evenly.

If a new element beats (2) in the squint test, it is too loud.

---

## 3. Colour

### 3.1 Neutrals (warm graphite; hue ≈ 60–80°, chroma near zero)

| Token | Hex | Use |
|---|---|---|
| `ink0` | `#0C0C0B` | deepest wells: meter troughs, QS lane, knob track underlay, entry field |
| `ink1` | `#121211` | display wells (editor, graphs, fields, shape bank) |
| `ink2` | `#181816` | overlay panels (preset browser) |
| `ink3` | `#1E1D1B` | menus and popovers |
| `ink4` | `#262523` | raised keys (buttons, lamp keys) |
| `ink5` | `#2F2E2B` | selected segment key, selected list row, dividers in wells |
| `ink6` | `#3A3935` | knob track, menu border, slider cap |
| `ink7` | `#4A4843` | minor scale ticks, inactive graph lines, hovered slider cap |
| `textDim` | `#7E7A72` | tertiary: help copy, axis ticks, captions (never needed to operate) |
| `textMute` | `#99958B` | labels, section titles, idle icons |
| `text` | `#D5D0C5` | values, neutral knob arcs, idle key text on hover |
| `textHi` | `#F2EEE4` | active and selected text, pointers, emphasis |

**Chassis.** Vertical gradient `#22211F` (y = 0) → `#1B1A18` (y = 660), plus grain (§5.1).
Header rail: gradient `#1A1A18` → `#171715`. Footer rail: `#171715`.
**Light hairlines** use `rgba(255,248,235,a)` (warm white), never pure white.
**Shadows** are black at the stated alpha.

### 3.2 Semantic hues

| Token | Hex | Meaning (and only this meaning) |
|---|---|---|
| `duck` | `#FFB81C` | the envelope and the amount of ducking: curve, nodes, Depth ring, live GR arc, spectral cut bars, Low band, Snap / Quick shift / Split / Delta / bypass-engaged lamps, active library tab, selected library cell |
| `duckHi` | `#FFD066` | hover tint of duck marks |
| `high` | `#4DB2F5` | the high band: its curve, tab swatch, High-mix arc, crossover high-pass, Solo H |
| `highHi` | `#8FD0FA` | hover tint of high marks |
| `kick` | `#FF4A3D` | the sidechain / trigger: Side waveform, trigger LED, threshold meter, detection-filter response, spectral sidechain line, MIDI activity, Capture, armed/recording states |

Rules:

* A hue never appears as decoration. If an element isn't *about* the duck, the high band or the
  sidechain, it's neutral.
* Neutral knob arcs (Hold, Attack, Release, Range, Sens, Velocity, Output) use `text`.
* Depth, Low mix = `duck`; High mix = `high`.
* There's no "error red". Nothing in the plugin can be in error. Clipping isn't displayed, and
  capture failure falls back to idle.

### 3.3 Data colours in the editor

| Layer | Fill | Stroke |
|---|---|---|
| Out waveform (after ducking) | `rgba(245,240,230,0.075)` | none |
| In waveform (before) | `rgba(245,240,230,0.07)` if Out is hidden | 1 px `rgba(245,240,230,0.20)` top edge if Out is shown |
| Side waveform (kick) | `kick` @ 0.10 | 1 px `kick` @ 0.55 top edge |
| Envelope fill | vertical gradient `band` @ 0.085 (plot top) → @ 0 (plot bottom), under the curve | — |
| Envelope | — | 2 px `band`, round joins and caps |
| Effective curve (Depth or band mix < 100 %) | — | 1 px `band` @ 0.55, dash 3/3 |
| Other band's curve (Split, unlinked) | — | 1.25 px other band @ 0.42 |
| Library hover preview | — | 1.25 px `textHi` @ 0.55, dash 4/3 |
| Recording sweep | `kick` @ 0.07 from plot left to progress | 1.5 px `kick` @ 0.8 at progress |

`band` = `duck` when editing Low or single-band, `high` when editing High.

### 3.4 States

| State | Treatment |
|---|---|
| Idle key | `ink4`, text `textMute` |
| Hover | key fill mixes 45 % towards `ink6`; text eases towards `text`; hover fades take about 80 ms |
| Pressed / active drag | same as hover; knob arcs lighten 25 % towards white while hovered or dragged |
| Selected (segment, tool) | raised `ink5` key, text `textHi`, weight 620 |
| On (lamp) | lamp lit in its hue with bloom; label `textHi` |
| Disabled / inactive | 38 % global alpha on the group (e.g. Bands when Split is off); text `textDim`; no hover |
| Keyboard focus | There is no Tab-order traversal, because hosts own Tab. After a click the editor owns the keyboard (Delete, arrows, Ctrl+A). The value-entry field (below) is the only element drawn as focused. If a future build adds traversal, use a 1 px `textHi` ring 2 px outside the bounds |
| Value entry | field `ink0`, 1 px `duck` border, text `textHi`, selection `duck` @ 0.30, caret 1.2 px `duck` blinking 560/440 ms |

### 3.5 Grain, gradients, alphas

Grain opacity **0.16** (§5.1). The only colour gradients in the product are:

* chassis and rail lighting,
* the well's inner shadow,
* the envelope fill,
* lamp bloom,
* the threshold meter fill (`kick` @ 0.35 → `kick`),
* the playhead trail (18 px, `rgba(245,240,230,0.045)` → 0).

### 3.6 Contrast (WCAG 2.x ratios, measured)

| Foreground | on chassis `#1F1E1C` | on well `ink1` | on key `ink4` | on selected `ink5` |
|---|---|---|---|---|
| `textDim` | 3.9 | 4.4 | 3.6 | 3.2 |
| `textMute` | 5.6 | 6.3 | 5.1 | 4.5 |
| `text` | 10.8 | 12.2 | 10.0 | 8.8 |
| `textHi` | 14.4 | 16.2 | 13.2 | 11.7 |
| `duck` | 9.6 | 10.8 | 8.8 | 7.8 |
| `high` | 7.1 | 8.0 | 6.6 | 5.8 |
| `kick` | 5.0 | 5.6 | 4.6 | 4.1 |

All labels and values meet AA (4.5:1). All operative graphics (arcs, curves, handles) meet the
3:1 non-text rule. `textDim` is reserved for non-essential text ≥ 9.5 px. Knob tracks (`ink6`)
are deliberately low; the value arc carries the information.

### 3.7 Colour-vision safety

Simulated with Machado 2009 (severity 1.0), OKLab ΔE × 100 (≈ 2 is just noticeable):

| Pair | Normal | Protan | Deutan | Tritan |
|---|---|---|---|---|
| duck / high | 31 | 27 | 31 | 24 |
| duck / kick | 24 | 25 | 16 | 23 |
| high / kick | 35 | 28 | 25 | 39 |

A second cue backs up every hue distinction:
* the bands have text labels (tabs, "L"/"H");
* the sidechain is always a trace or line and never has nodes;
* Quick Shift members are **diamonds**, not circles;
* selected nodes are **filled** `textHi`.

---

## 4. Typography

### 4.1 Fonts

**Archivo** by Omnibus-Type (SIL OFL 1.1, no Reserved Font Name), static instances generated by
`tools/assets/fonts.py`:

| File (resources/fonts/) | Axes | Runtime name | Role |
|---|---|---|---|
| `ArchivoSC-Medium.ttf` | wdth 87.5, wght 500 | `sc` (500) | labels, menus, buttons, hints |
| `ArchivoSC-SemiBold.ttf` | wdth 87.5, wght 620 | `sc` (620) | values, selected states, titles, preset name |
| `ArchivoExp-SemiBold.ttf` | wdth 125, wght 600 | `exp` | hero numerals: Depth value, rate |

Each file is about 32 KB (Latin-1 plus typographic punctuation: − ° × ± • … – — ‘ ’ “ ” ′ ″ ⁄).
Hinting is stripped because stb_truetype doesn't hint. GPOS pair kerning is kept:
stb_truetype ≥ 1.19 reads PairPos formats 1 and 2 with XAdvance, which is what Archivo uses.

**Why Archivo.** It isn't one of the reflex fonts. It has a large x-height and open counters
that survive 11 px on dark backgrounds. Its **default figures are tabular** (all digits ≈ 576
units), so values never jitter even though NanoVG can't switch on `tnum`. The width axis gives
one family three voices: compact for labels, expanded for heroes, and extra-bold expanded for
the wordmark.

### 4.2 Roles

| Role | Font | Size | Colour | Notes |
|---|---|---|---|---|
| Section title | sc 620 | 11.5 | `textMute` | sentence case, left-aligned |
| Label | sc 500 | 11 | `textMute` | under knobs (cy + ring + 12), left of fields |
| Value | sc 620 | 11.5 | `text` → `textHi` on hover | under labels (cy + ring + 26); underline on hover means "click to type" |
| Hero value | exp 600 | 17 (Depth), 19 (rate) | `textHi` | |
| Preset name | sc 620 | 12.5 | `text` (hover `textHi`) | preceded by category, 11.5 `textDim`, and " / " |
| Dirty marker | sc 620 | 13 | `duck` | `*` after the name, raised 1.5 px |
| Segment text | sc 500 / 620 when selected | 11 | `textMute` / `textHi` | |
| Axis and scale labels | sc 500 | 9.5–10 | `textDim`; beat lines `textMute` | |
| Menu item | sc 500 | 11.5 | `text` | 22 px rows; menu headers 10.5 `textDim` |
| Hint line | sc 500 | 10.5 | `textMute` (hovering), `textDim` (idle) | |
| Drag readout tag | sc 620 | 10.5 | `textHi` | |

* No all-caps. Letter-spacing is always 0.
* Minus signs are U+2212 (−), never hyphens.
* Units get a thin gap: "−24.0 dB", "100 %".

### 4.3 Number formats

| Parameter | Format |
|---|---|
| Depth, Mix, Swing, Smooth, Velocity, Sens | `87 %` |
| Rotate | `90°` |
| Output | `+1.5 dB`, `0.0 dB`, `−6.0 dB` |
| Threshold | `−24.0 dB` |
| Range | `18.0 dB` |
| Frequencies | `150 Hz`, `2.26 kHz`, `12.5 kHz` |
| Times | `0.10 ms`, `5.0 ms`, `120 ms`, `1.25 s` |
| Mid/Side | `Mid + Side` at 0, otherwise `Mid 40 %` / `Side 60 %` |
| MIDI note | `Any note`, or `C1  ·  36`, with C3 = 60 (the Ableton/Bitwig convention; see open questions) |

### 4.4 Wordmark

`resources/images/logo_wordmark.json` holds path data made from Archivo wdth 125 / wght 800,
lower case, tracking −1 %. The letters `a r s e` are offset down by **0.36 / 0.24 / 0.12 / 0
x-heights** (a linear release), so the word draws a duck.

The file contains, in em units, y-down, baseline at 0:
* contours of `["M",x,y]`, `["L",x,y]`, `["Q",cx,cy,x,y]`, `["Z"]` commands,
* a `hole` flag per contour,
* bbox `[0.065, −0.725, 5.186, 0.201]`.

Drawing it in NanoVG:

```
nvgSave; nvgTranslate(16 - 0.065*21, 27); nvgScale(21, 21); nvgBeginPath;
for each contour: moveTo / lineTo / quadTo …; closePath; nvgPathWinding(hole ? NVG_HOLE : NVG_SOLID)
nvgFillColor(textHi); nvgFill; nvgRestore
```

Also provided: `logo_wordmark.svg`, and `logo_wordmark@2x.png` (installer or about box).
Minimum size is 14 px em; below that, drop the duck offsets. Clear space is 0.5 em on every side.

---

## 5. Materials and lighting

**One key light, upper left, above the panel.** Direction (screen space, y down, z towards
the viewer): **(−0.34, −0.78, 0.90)**. Everything obeys it:
* top edges catch light,
* bottom edges and shadows fall down and very slightly right,
* recesses darken at the top,
* bosses darken at the bottom.

### 5.1 Chassis

This is powder-coated steel, not a texture.
* Gradient per §3.1.
* **Grain:** `resources/images/grain_256.png`, a tileable signed noise tile. White pixels
  lighten and black pixels darken, with alpha = |v| × 0.16.
* Draw it over the whole canvas with global alpha 0.16:

```
paint = nvgImagePattern(0, 0, 256/k, 256/k, 0, grainImg, 0.16)
```

  where `k` is the current total scale, so one grain texel = one device pixel.
* Load the image with `IMAGE_REPEAT_X | IMAGE_REPEAT_Y` and no mipmaps.

### 5.2 Groove (section separator)

A 1 px `rgba(0,0,0,0.55)` line at the stated coordinate, plus a 1 px `rgba(255,248,235,0.045)`
line directly below it (horizontal) or to its right (vertical). Draw both at +0.5 px for crisp
1× rendering.

### 5.3 Well (recessed display: editor, graphs, fields, shape bank)

1. Fill the rounded rect (radius 3) with `ink1`.
2. **Inner top shadow.** Fill the *same* rounded-rect path with
   `nvgLinearGradient(x, y, x, y+6, rgba(0,0,0,0.55), rgba(0,0,0,0))`. The gradient clamps
   outside its span, so no scissor is needed.
3. **Catch-light.** A 1 px line from (x+r, y+h−0.5) to (x+w−r, y+h−0.5) in
   `rgba(255,248,235,0.055)`.

### 5.4 Raised key (buttons, lamp keys, selected segments)

1. Contact shadow: rounded rect offset +1 px down in `rgba(0,0,0,0.35)`.
2. Body fill (`ink4`, or `ink5` for the selected segment).
3. Top light: 1 px line from x+r to x+w−r at y+0.5 in `rgba(255,248,235,0.075)`.

Radius 3 for keys, 2 for keys inside a segmented track.

### 5.5 Lamp (LED)

A 5 × 5 px square with radius 1.5.
* **Off:** fill `ink0`, 1 px inner border `rgba(255,248,235,0.08)`.
* **On:** fill the hue; 1 px top highlight `rgba(255,255,255,0.55)`; bloom from
  `nvgRadialGradient(cx, cy, 0, 9, hue @ 0.35·level, hue @ 0)` over a 20 × 20 square.

Lamps are the *only* glowing things on the chassis. Lamp level can animate: the trigger LED
decays from 1 to 0 in 200 ms.

### 5.6 Drop shadows (menus, overlays, slider cap)

Use NanoVG's standard shadow:

```
nvgBoxGradient(x, y+dy, w, h, r, feather, rgba(0,0,0,a), rgba(0,0,0,0))
```

Fill it over a rect expanded by `feather`, with the body as a hole.

| Element | dy | feather | a |
|---|---|---|---|
| Menus | 6 | 18 | 0.5 |
| Preset browser | 8 | 24 | 0.55 |
| Slider cap | 1.5 | 4 | 0.5 |

The prototype's `boxGradient()` reproduces the NanoVG ramp exactly.

### 5.7 Knob bodies (rasters)

Rendered by `tools/assets/knobs.py` with the key light above plus a cool fill from the lower
right and a softbox environment. Only the body is baked; pointers, rings and ticks are vector.
The light doesn't rotate with a real knob either, so the body image **never rotates**.

| File | Logical size | Body Ø | Materials |
|---|---|---|---|
| `knob_hero@2x.png` (192 × 192) | 96 × 96 | 72 | glazed bone ceramic cap (dished top, tapered wall, 0.17 R fillet) on a spun gunmetal collar |
| `knob_small@2x.png` (88 × 88) | 44 × 44 | 30 | soft-touch graphite rubber skirt, spun gunmetal insert (0.64 R) with a dark seam |

The PNG includes the baked contact shadow and AO as black + alpha, so it composites onto any
chassis tone. Draw it centred on the knob with `nvgImagePattern` over its logical rect, with
the image loaded with `IMAGE_GENERATE_MIPMAPS`.

---

## 6. Components

### 6.1 Knob (small), 30 px body

| Part | Spec |
|---|---|
| Hit area | ring bounds + 4 px (a 48 × 48 square) |
| Track | arc at r = 20 from A0 = 0.75π, sweep 1.5π. Draw first at w = 4 in `ink0` (underlay), then at w = 2 in `ink6`; round caps |
| Value arc | w = 2, round caps. Unipolar parameters draw from the minimum. **Bipolar** ones (Output) draw from the default and add a 1 px `textDim` notch at the default angle (r ± 3) |
| Arc colour | `text` (neutral), `duck` (Depth, Low mix), `high` (High mix). Lightens 25 % towards white on hover or drag |
| Body | image 44 × 44 centred |
| Pointer | line from r = 4 to r = 12.5 at angle A0 + 1.5π·n; 2 px, round caps, `textHi` |
| Label / value | label at cy + 32; value at cy + 46 (§4.2) |
| Dimmed | the whole group at 38 % global alpha (Split off) |

### 6.2 Knob (hero: Depth)

| Part | Spec |
|---|---|
| Ring | r = 50, w = 3, `duck`; underlay w = 5 in `ink0`; track `ink6` |
| Scale ticks | 21 ticks every 5 %. Minor: r 55→57, 1 px `ink7`. Major (every 25 %): r 55→60, 1.25 px `textDim`. End labels "0" / "100" at (cx ∓ 44, cy + 52), 9.5 px `textDim` |
| **Live duck arc** | r = 44, w = 1.5, `duck` @ 0.8, round caps, from A0 to A0 + 1.5π·(depth·(1 − valueA)). It shows how much is being ducked *right now*, chasing below the setting |
| Body | image 96 × 96 centred |
| Engraved pointer | dark line r 9 → 27, 3.2 px `rgba(38,34,29,0.92)`, round caps; highlight line offset (+0.7, +0.9), 0.9 px `rgba(255,255,255,0.28)`. The lower-right wall of a groove catches the upper-left light |
| Value | centre (980, 222), exp 17 px `textHi`; click to type |
| Drag | 260 px = full range (finer than small knobs) |

### 6.3 Lamp button

A raised key (radius 3, `ink4`), height 22–30.
* Lamp at (x + 11, cy).
* Label at x + 20. With an icon: icon centred at x + 26 (scale 0.85) and the label at x + 35.
* Label 11 px: `textHi` when on; `textMute` → `text` on hover when off.
* Lamp hue follows the parameter's meaning: duck for Snap / Quick shift / Split / Delta /
  bypass-engaged / In / Out, kick for Side / Filter / Listen, neutral for Learn.
* Width = label width + 28 (+ 15 with an icon).

### 6.4 Segmented control

* Track: a well (radius 3). Height 20–26.
* Segment widths are proportional to label widths, with the spare space shared equally, so
  "Spectral" gets more room than "Ring". Use `fixed` for equal splits (Note/ms, Loop/One-shot,
  Depth/Volume, slope).
* Selected segment: raised key (radius 2) at (x, y+2, w, h−4) in `ink5`, text 620 `textHi`.
* Unselected: text `textMute`, hover `text`.
* Optional per-segment icon (loop / one-shot glyphs at 0.8 scale, before the text).

### 6.5 Step selector (Rate)

* Well 212 × 38.
* Chevron buttons 32 × 34 at both ends; the left one lengthens the cycle.
* Centre value: exp 19 px, with a small caret 10 px right of the text meaning "click for grid".
* Wheel steps one entry; the chevrons clamp at the ends.
* Clicking the value opens the **rate grid** (§6.8).
* **ms mode:** the stepper becomes a value field "Length … 250 ms" (drag, log mapping 5–4000 ms).

### 6.6 Value field (Swing, Smooth, Rotate, Length)

* Well, height 24–28.
* Label 11 px `textMute` at x + 8; value 11.5 px/620 right-aligned at x + w − 8.
* On hover, a 1 px inner outline `rgba(255,248,235,0.08)`.
* Behaves exactly like a knob (§9.1): vertical drag, Shift fine, double-click reset, Alt-click to
  type, wheel, right-click menu.

### 6.7 Dropdown (Grid) and menus

* The dropdown is a well: label left, value right, caret at the far right. Click opens a menu;
  wheel steps.
* **Menu panel:**
  * `ink3` fill, 1 px `ink6` border, radius 4, shadow per §5.6.
  * Min width 150 (or text + 44). Rows 22 px, inset 4.
  * Hover row `rgba(255,248,235,0.07)`, radius 3.
  * Check mark: 1.5 px `duck` polyline (12, 11) → (15, 14) → (20, 7.5).
  * Separator: 1 px `ink5` inset 8, 9 px block.
  * Headers 10.5 px `textDim`, not interactive.
* Menus open under their anchor, right-aligned to it where there's no room (settings), or
  above it (footer).
* A click outside closes the menu without acting. Esc closes it.

### 6.8 Rate grid (popover)

* Panel as §6.7.
* Grid of **3 rows** (Straight / Dotted / Triplet, labels in a 60 px column) × **9 columns**
  (4/1 2/1 1/1 1/2 1/4 1/8 1/16 1/32 1/64).
* Cells 42 × 26. Cells that don't exist in `kRates` show a 2 × 1 `ink6` dash.
* Header row: "Cycle length", "bars" over the first two columns, "beats and below".
* Selected cell: `ink5` fill with `duck` text 620. Hover `rgba(255,248,235,0.06)`.
* The hint shows ms at the current tempo.

### 6.9 M/S slider

* Track 3 px `ink0`, radius 1.5, from x+8 to x+w−8 at y+12.
* Centre detent tick: 1 px `ink7`, ±6 px.
* Fill from centre to the cap: 2 px `textMute`.
* Cap: 10 × 16, radius 2, shadow (§5.6), fill `ink6` (hover `ink7`), 1 px `textHi` centre line
  ±4 px.
* End labels "Mid" / "Side" at y+30. The active side is `text`, otherwise `textDim`.
* Horizontal drag: the track width = 200 units. Double-click or Ctrl-click → centre.

### 6.10 Icon buttons and the icon set

* Hit area 24–32 × 24–28; hover plate `rgba(255,248,235,0.05)`, radius 3.
* Icon colour: `textMute` → `textHi` on hover, `duck` when "on", `textDim` when disabled.

**Icons.** 14 px grid centred on (0,0), 1.5 px strokes with square caps unless noted. Units are
px at scale 1.

| Name | Geometry |
|---|---|
| select | filled arrow: (−3.5,−6) (−3.5,5) (−0.8,2.4) (1.3,6.5) (3,5.6) (1,1.6) (4.5,1.6) |
| line | stroke (−4.5,4.5)→(4.5,−4.5); filled 4 × 4 squares at (−6.5,2.5) and (2.5,−6.5) |
| pencil | closed outline (−5.5,5.5) (−5,2.5) (3,−5.5) (5.5,−3) (−2.5,5), butt caps, round joins; ferrule (1,−3.5)→(3.5,−1) |
| prev / next | chevron ±(2,−4.5)→(∓2.5,0)→(±2,4.5), round caps and joins |
| caret | filled triangle (−3.5,−1.5) (3.5,−1.5) (0,2.5) |
| browse | three rows at y = −4, 0, 4: a 2 × 1.5 bullet at x −5.5 and a 7.5 × 1.5 bar at x −2 |
| save | arrow (0,−6)→(0,1.5) with head (−3.2,−1.5)(0,1.7)(3.2,−1.5); tray (−5.5,1.5)(−5.5,5.5)(5.5,5.5)(5.5,1.5); butt caps |
| star / starOn | 10-point star, outer r 6, inner r 2.6, rotated −90°, y + 0.4; outline 1.3 px or filled |
| undo / redo | hook: (−5,−2)→(2,−2), quad to (5.5,1.5), quad to (2,5)→(−1,5); head (−2,−5.5)(−5.5,−2)(−2,1.5); redo is mirrored in x |
| more | three filled dots r 1.3 at x −5, 0, 5 |
| power | arc r 5 centred (0,0.8), open at the top ±0.28π; stem (0,−6)→(0,−0.5); round caps |
| link | two capsules 8.4 × 5.2 (radius 2.6) at x −7 and −1.4, overlapping by 3, rotated −45°, 1.4 px |
| unlink | the same capsules separated (x −8.2 and 1.6, w 6.6) plus two 1.2 px break ticks |
| phones | headband arc r 5.2 centred (0,1.5) from 1.08π to 1.92π, 1.5 px butt; filled cups 3.4 × 5.8 (radius 1.2) at (−6.6,0.2) and (3.2,0.2) |
| rec / stop | filled circle r 4 / filled 7 × 7 square |
| loop | two opposing rounded arrows, 1.4 px (see prototype `icon('loop')`) |
| oneshot | arrow (−6,0)→(3,0) with head, and a 1.6 × 10 end bar at x 4.5 |
| delta | triangle outline (0,−5.5)(5.8,4.8)(−5.8,4.8), 1.4 px, miter |
| plus / close | ±5 cross / ±4.5 X |
| grip | three 1 px diagonals, offsets 0 / 3.5 / 7 from the corner (5,5) |
| folder, search | 1.4–1.5 px outlines (see prototype) |

The exact paths are in the prototype's `icon()` switch. Port them verbatim to `Icons.cpp`.

### 6.11 Value entry popover

* Opens in place of the value text; width = max(56, text + 18), height 20, radius 3.
* Styling per §3.4.
* Opens with the current value selected. Digits, `.`, `-` and `k` are accepted ("2.5k" → 2500).
  Backspace edits, Enter commits (clamped, one undo step), Esc cancels, and a click elsewhere
  commits.
* Keyboard input comes from DGL `onCharacterInput` / `onKeyboard`.

### 6.12 Context menus

Right-click on:

| Target | Items |
|---|---|
| Any value control | Reset to default (*value*), Type a value…, —, Copy value, Paste value (dimmed when the clipboard is empty) |
| Envelope node | Delete node, Add to / Remove from quick shift, Straighten segment, Step here (vertical jump). Endpoints dim the items that don't apply to them |
| Editor background | Select all, Flip vertically, Mirror horizontally, Reset to default duck, —, Save shape to User… |
| Library cell | Add to / Remove from favourites, Rename…, Delete (User only) |

### 6.13 Hint line

The footer's left side (x 16, 10.5 px). Shows the hovered control's name, current value and
gestures, e.g. "Depth 87 % — drag · Shift fine · double-click reset · Alt-click type ·
right-click menu". Idle text lists the global shortcuts in `textDim`. It can be turned off in
Settings → Hint line. There are no pop-up tooltips.

### 6.14 Meters and graphs

**Threshold meter** (Audio mode: 212 × 44; compact in Spectral/Ring: 148 × 22).
* Well with an inner scale of −60…0 dB over x+6…x+w−6.
* Trough 6 px `ink0`. Peak fill is a horizontal gradient `kick` @ 0.35 → `kick`.
* The region above the threshold is darkened with `rgba(0,0,0,0.45)`.
* Threshold line: 1.5 px `text` (hover `textHi`) with a 8 × 5 downward triangle at the top.
* Ticks −48, −36, −24, −12, 0 (9.5 px `textDim`, full size only).
* The trough flashes `kick` @ 0.25 on each trigger.
* Gestures: click sets the threshold to that point, drag is relative (Shift fine), wheel ±1 dB,
  double-click resets to −24 dB.

**Detection-filter graph** (212 × 64).
* Log axis 20 Hz–20 kHz; gridlines at 100 / 1k / 10k with labels.
* Band-pass magnitude (4th-order shape preview) filled `kick` @ 0.13 with a 1.5 px `kick`
  stroke.
* When Filter is off: flat line in `ink7`, fill @ 0.04.
* Two handles (6 × 10, radius 1.5) at the low-cut and high-cut frequencies. The value shows next
  to a handle while it is hovered or dragged. Dragging a handle also turns Filter on.

**Crossover graph** (168 × 66).
* Log axis; low-pass curve in `duck` and high-pass in `high` (LR magnitude, order 2 or 4 by
  Slope). Fills @ 0.12. A soloed-away band drops to @ 0.03 fill and @ 0.35 stroke.
* Split line: 1 px `rgba(245,240,230,0.55)` (hover `textHi`) with a 6 × 9 handle at the top.
* Frequency label 10.5 px/620 `textHi` next to the handle; it flips side near the right edge.
* Drag is **relative** (grabbing never jumps), Shift fine, wheel ±3 %, double-click → 150 Hz.
* The whole graph sits at 40 % alpha when Split is off.

**Spectral display** (212 × 84). See §8.4.

**GR meter.** 66 × 5 trough `ink0`, fill `duck` from the right edge leftwards over 0–36 dB,
caption "GR −4.2" in 10 px `textDim`.

---

## 7. The envelope editor

### 7.1 Space and mapping

* The plot (268, 100, 588, 272) shows **timeline** phase 0…1 left to right, and envelope value
  y 0…1 bottom to top.
* Draw through `PhaseMap` so the editor shows what is heard, including rotate and swing.
  * **Nodes** are stored in node space and drawn at `X(toTimeline(node.x))`.
  * **The curve** is sampled per screen column: p → `toNode(p)` → evaluate.
* **End of cycle.** `Envelope::evaluate(1.0)` *wraps* to the first node, per the contract. When
  drawing, evaluate segments directly with `Envelope::shape()`, and when rotate = 0 draw the
  node at x = 1 at the right edge (`toTimeline(1) → 1`).
* `divisions` = cycleBeats / gridBeats in Note mode; 1 / gridBeats in ms mode, where the grid
  divides one cycle.
* The editor's inner clip is the plot inflated by 8 px, so edge nodes and their halos aren't
  cut. Use a scissor, which is rectangular.

### 7.2 Background and grid

* The well (§5.3), plus a vertical wash over it: `rgba(255,248,235,0.012)` at the top →
  `rgba(0,0,0,0.18)` at the bottom.
* **Horizontal lines:** at y = 0.25, 0.5, 0.75 in `rgba(255,248,235,0.035)`; at y = 1 (unity)
  and y = 0 in `rgba(255,248,235,0.07)`.
* **Vertical lines**, one per grid cell, at the *timeline* positions of k / divisions (so swing
  visibly shifts every second line):
  * cycle start: `rgba(255,248,235,0.10)`
  * cells on a quarter-note beat: 0.075
  * other cells: 0.035
* **Axis labels** in the label strip (centre-y 381, 10 px):
  * Note mode: `k/den` (e.g. "1/16 2/16 3/16"), or `bar.beat` ("1.2", "1.3", "2.1") on whole
    beats when the cycle is longer than a beat.
  * ms mode: "90 ms".
  * Decimation: label every ⌈divisions / 16⌉-th line. Beat labels `textMute`, others
    `textDim`.
* **Y caption** at the strip's left edge, 10 px `textDim`. It names what the y axis scales:
  *Gain* (Sync/MIDI/Audio), *Spectral depth* (Spectral → Depth), *Gain after spectral*
  (Spectral → Volume), *Ring depth* (Ring).
* No numeric y labels. The drag readout gives dB.

### 7.3 Waveform layers (behind the curve)

These are a **magnitude view on the gain axis**: each bin's peak |x| (linear) is drawn upward
from the plot bottom, bin i at x = i/512.
* **Out** (post-duck) is filled. **In** (pre) is an outline when Out is shown, otherwise filled.
* **Side** (the sidechain after the detection filter) is a red fill plus edge (§3.3).
* Because Out = In × gain, the output silhouette *takes the shape of the curve*, and the space
  between In's outline and Out's fill is exactly what was removed.
* Display smoothing: take the max over a 3-bin window, then a one-pole release at 30 dB/s per
  bin. The DSP writes raw peaks.
* Toggles: In / Side / Out (toolbar), default all on.

### 7.4 Curve and nodes

**Curve.** Per §3.3. The width is 2 px at every scale; stroke width scales with the transform.

**Nodes:**

| State | Radius | Spec |
|---|---|---|
| Idle | 4 | fill `ink1`, 2 px band ring |
| Hover or dragging | 5 | fill band; bloom `nvgRadialGradient` r 0 → 12, band @ 0.28 → 0 |
| Selected | 4.5 | fill `textHi`, band ring |
| Quick-Shift member | — | same states, drawn as a **diamond** with half-diagonal r + 0.8 |
| Vertical jump | — | two nodes at the same x; the curve is a vertical line between them |

**Tension handles** sit at the curve point of each segment's node-space midpoint. They're
visible only for the hovered segment, segments touching a selected node, and the segment being
bent. They are **solid dots** (r 2.5, band @ 0.75), so they never read as nodes. Hot: r 3.5,
band fill, 1.5 px `ink1` ring.

**Curve law.** Match `Envelope::shape(u,t) = (e^{6tu} − 1)/(e^{6t} − 1)`: t > 0 eases in
(holds the start value longer). **Dragging a segment or handle up raises the curve.** For a
rising segment that means t decreases; for a falling one t increases. 90 px of drag = 1.0
tension; Shift × 0.25. The readout shows "Curve +45".

### 7.5 Playhead, trigger, one-shot

* **Playhead** at `X(bridge.phase)`: 1 px `rgba(245,240,230,0.5)` over the full plot height,
  rounded to the device pixel + 0.5. At 0.18 alpha while `envelopeActive == 0`.
* Trail: an 18 px gradient to the left.
* **Riding dot:** r 3.5, `textHi` fill, 2 px `ink1` ring, at the curve value under the playhead.
* **Trigger flash** (non-Sync modes): when `triggerCount` changes, draw a 2 px vertical bar at
  the timeline cycle start in `kick` @ 0.5·level, with level decaying 1 → 0 in 200 ms. The
  Trigger LED flashes at the same time.
* **Loop / one-shot glyph** at the plot's top-right (10 px in), 0.8 scale, `textDim`.
* **Waiting states**, right-aligned next to the glyph (11 px `textDim`):
  * One-shot finished, or waiting for a trigger: "Held — waiting for the next trigger".
  * Loop, no triggers yet: "Waiting for a trigger".

### 7.6 Quick Shift lane

The lane (268, 392, 588, 16) is an `ink0` trough, radius 2.

* **Group bar:** spans the timeline range of the QS nodes, from (x0, y+2) to x1, height 12,
  radius 2, band @ 0.20 (@ 0.32 on hover or drag).
  * Top edge 1 px band @ 0.6.
  * Centre grip: three 1 px lines at ±3 px, `textHi` @ 0.55.
  * End handles 3 px wide, band @ 0.6 (1.0 when hovered).
* While hovering or dragging, show guides in the plot: fill band @ 0.05 over the range and
  1 px band @ 0.3 at both ends.
* **Drag the bar** to move every QS node by the same Δx.
  * The group is clamped between the nearest non-group neighbours (−0.001 each side).
  * It snaps to ¼ grid cell when Snap is on; Ctrl inverts that; Shift × 0.25.
  * Readout above the lane: "+12.5 ms".
* **Drag an end handle** to change the range. Every non-endpoint node inside the range becomes
  a member (sets `kNodeQuickShift`).
* **Ctrl-click a node** in the plot toggles its membership. Endpoints can't be members.
* Empty state (no members): centred text "Quick shift: Ctrl-click nodes to group them", 10 px
  `textDim`.
* The footer toggle "Quick shift" shows or hides the lane and the diamond styling.

### 7.7 Multiband curves

When Split is on and **unlinked**:
* Band tabs `[■ Low] ⛓ [■ High]` appear at the start of the editor footer. The swatch is a
  6 × 6 square in the band colour (@ 0.35 when not selected).
* The edited band's curve is full (colour, fill, nodes); the other is a 1.25 px ghost at 0.42
  with no nodes.
* The waveform layers stay neutral.

When **linked** (the default): one curve in `duck`, both tabs styled as selected, and the link
icon lit.
* Unlinking copies the current envelope to envB so nothing jumps.
* Linking makes the high band follow envA. envB is kept but inactive; relinking doesn't delete
  it until the next save.

### 7.8 Tool previews

* **Marquee:** fill `rgba(245,240,230,0.05)` with a 1 px `rgba(245,240,230,0.4)` crisp outline.
* **Line tool:** while dragging, a 1.5 px `textHi` line with 3.5 px band dots at both ends.
  * On release, nodes strictly inside the x-span are removed and nodes are inserted at both
    ends, snapped when Snap is on.
  * Endpoints inside the span only get their y set.
  * Segments get tension 0.
* **Pencil:** a freehand 1.5 px `textHi` polyline, clamped to the plot.
  * On release, resample the stroke at 97 points across its x-span, replace nodes in the span
    with the RDP simplification (tolerance 0.012 in y), and insert. In C++ use
    `Envelope::fromSamples` over the span.
* **Library hover preview:** a dashed ghost of the hovered shape (§3.3). Nothing is applied
  until you click.
* **Drag readout tag:** 18 px tall, `rgba(12,12,11,0.92)` fill, 1 px `ink6` border, radius 3.
  Positioned at cursor + (10, −26), clamped inside the plot.
  * Node drag: "42.0 ms   −8.4 dB". The dB value is **at the current Depth**, i.e. what is
    heard.
  * Bend: "Curve −35".

### 7.9 Recording (audio → envelope)

The flow follows the Bridge handshake:

| Bridge state | Editor | Capture key |
|---|---|---|
| idle | — | "● Capture", red dot |
| **armed** (UI stored `kRecArmed`) | 1.5 px `kick` border around the display well, pulsing alpha 0.35 ↔ 0.75 (period ≈ 1.4 s); top-left text "Armed — capture starts on the next cycle / trigger" in `kick` | "■ Armed", red square blinking on a 180 ms rhythm; click cancels (store `kRecIdle`) |
| **recording** | sweep fill and line (§3.3) following `recProgress`; text "Capturing sidechain… 46 %" | key background fills left→right with `kick` @ 0.22 |
| **done** | UI builds the envelope (invert and normalise `recBuf`, then `fromSamples`), pushes one undo step, morphs to it (§11), saves it as the first User shape and shows "Captured from the sidechain — saved to User shapes" (fades over about 3 s) | returns to idle |

---

## 8. Per-mode panels (left column, origin (16, 248), 212 wide)

Only the active mode's panel is drawn. Switching modes is instant (no animation). The mode
labels in the UI are **Sync, MIDI, Audio, Spectral, Ring** (the Params label "Ring Mod" is
shortened).

### 8.1 Sync

| y offset | Content |
|---|---|
| +6 | "Locked to the host transport." (`textMute`) |
| +26 | transport lamp (duck, lit while playing) + "Playing"/"Stopped" (620); right-aligned "124.0 bpm · 4/4" (`textDim`) |
| +52 | "One bar" label; right-aligned "4 cycles" (or "2 bars per cycle") |
| +62 | **bar view** well 212 × 70 |
| +150 | "Rate, under Time, sets how many fit in a bar." (`textDim`) |

The bar view:
* Inner plot 200 × 46.
* Beat lines at 1–4 (the bar line @ 0.10, others @ 0.05) with 9.5 px beat numbers.
* The envelope (envA) tiled across one 4/4 bar at the current rate: 1.25 px `duck` with a 0.08
  fill.
* A bar-position line (1 px @ 0.55) while playing.

It makes the rate tangible: 1/4 = four ducks per bar.

### 8.2 MIDI

| y offset | Content |
|---|---|
| +8 | "Note" label |
| +18 | note stepper well 142 × 30 (chevrons 24 × 26; value 12.5/620 centred: "Any note" or "C1 · 36"); **Learn** lamp key 62 × 30 (neutral lamp) |
| +58 | **two-octave keyboard** well 212 × 44 |
| +116 | caption: "Every note triggers. Pick a key to filter." / "Only C1 triggers." |
| +164 | Velocity knob (centre x+26) with a two-line caption at x+62 ("Note velocity scales / the depth of each duck.") |
| +230 | activity lamp (kick) + "Last note C1 · 36 · vel 104" (`textMute`) |

The keyboard:
* 14 white keys, black keys 0.64 × white width and 0.58 × height.
* Starts at the octave containing the selected note (C1 when set to Any).
* Colours: idle whites `#4E4B46` (hover `#6A665F`); selected note `duck`; all whites `#7C7870`
  when Any; the last received note flashes `kick`.
* C labels 9 px/620 on the white keys. Click a key → `midiNote`.

Double-click the note value → Any.

### 8.3 Audio

| y offset | Content |
|---|---|
| +6 | "Threshold" label; value right-aligned (click to type) |
| +16 | threshold meter 212 × 44 (§6.14) |
| +100 | Hold knob (centre x+26), caption "Ignores new hits for / this long after a trigger." |
| +158 | row: "Detection filter" label, **Filter** lamp (kick, 56 w), **Listen** lamp (kick, headphone icon, 68 w) |
| +188 | detection-filter graph 212 × 64 |
| +268 | caption "20 Hz – 200 Hz" + right-aligned "isolates the kick in a full drum bus" (10 px) |

### 8.4 Spectral

| y offset | Content |
|---|---|
| +6 | "Driven by" label |
| +16 | source segmented **Audio · Sync · MIDI · Free** 212 × 24 (the Params label "Continuous" is shown as "Free") |
| +46 | only when the source is Audio: compact threshold meter (148 × 22) + value |
| next +4 | "Cut per band" label; right-aligned "up to −18 dB" in `duck` @ 0.85 |
| next +12 | **spectral display** well 212 × 84 |
| +108 | knob row: Attack, Release, Range, Sens (centres x + 26 + i·53.3) |
| +90 | "Envelope shapes" label + **Depth / Volume** segmented (118 × 24) |

The spectral display:
* 24 slots across x+6…x+w−6 with 2 px gaps.
* **Cut bars** (primary): `duck`, hanging from the top. Height = cut / Range × inner height, so
  a full bar = maximum cut. A 2 px slot cap `rgba(255,248,235,0.08)`.
* **Sidechain spectrum** (secondary): a polyline through the slot centres at
  (specScDb + 60)/60, 1.25 px `kick` @ 0.7, filled @ 0.10 to the baseline.
* Baseline hairline; frequency labels 100 / 1k / 10k (log 30 Hz–16 kHz).
* Display ballistics: instant attack, 250 ms release.

The hint explains the colours, so no legend is needed.

### 8.5 Ring

| y offset | Content |
|---|---|
| +6, +16 | "Driven by" + source segmented, as above |
| +46 | compact threshold meter when the source is Audio |
| then | three-line explanation in `textDim`: "The sidechain waveform multiplies the / main signal: the kick carves the bass / at audio rate instead of pumping it." |
| +56 | Attack and Release knobs at x + 50 and x + w − 50, with "faster" / "smoother" captions (10 px `textDim`) under them |

### 8.6 Multiband, Delta, Output, Bypass (right column)

* **Bands.**
  * **Split** lamp toggles `multi`.
  * The crossover graph, the slope segmented "12 | 24 dB", solo keys **L** / **H** (headphone
    icon plus a letter in the band colour when soloed; one solo at a time; click again to
    un-solo), Low mix and High mix knobs.
  * All dim to 38–40 % and stop reacting to hover when Split is off. They stay editable by
    drag so values can be prepared.
* **Stereo.** M/S slider (§6.9) with a value readout on the title row.
* **Output.**
  * Gain knob (bipolar from 0 dB); its value is printed beside it rather than under it.
  * **Delta** lamp (duck), "hear only what is removed". While Delta is on, the Out waveform
    toggle draws the *removed* signal instead (In − Out).
  * GR meter.
* **Bypass.** A header key with a lamp (duck, lit = *engaged*) and a power glyph. Bypassed
  state: lamp off, glyph `textMute`, and the editor's curve and playhead at 50 % alpha.

---

## 9. Interaction specification

### 9.1 Value controls (knobs, fields, sliders, meters)

| Gesture | Result |
|---|---|
| Drag vertically (horizontally for sliders, meters, graphs) | relative change: 200 px = full normalised range (hero 260 px); log parameters in log space |
| Shift while dragging or wheeling | ×0.1 |
| Double-click | reset to default |
| Ctrl-click (Cmd on Mac) | reset to default (FabFilter/Serum muscle memory) |
| Alt-click, or click the printed value | type a value (§6.11) |
| Wheel | ±2 % per notch (±0.2 % with Shift); enum controls step by one |
| Right-click | context menu (§6.12) |

* Every gesture brackets the host edit: `editParameter(i, true)` on mouse down,
  `setParameterValue` while dragging, `editParameter(i, false)` on release. Wheel steps and resets
  send a begin/set/end triple.
* One UI undo step per gesture.
* Hide the cursor during knob drags and restore its position afterwards *only if* DGL gains that
  ability. Not required.

### 9.2 Envelope editor (Select tool)

| Gesture | Result |
|---|---|
| Hover | cursor: move (node), up-down (segment or handle), arrow (empty); handles appear on the hovered segment |
| Drag a node | move; x snaps to the grid when Snap is on; y snaps magnetically to 0 and 1 within 1.5 %. **Ctrl** held = snap inverted; **Shift** = ×0.2 fine; **Alt** = lock to the dominant axis |
| Drag several selected nodes | all move together; order is kept by clamping against unselected neighbours |
| Drag a segment or its handle | bend (tension), §7.4 |
| Double-click empty space | add a node (snapped) and select it |
| Double-click a node / a handle | delete the node (endpoints never) / straighten the segment |
| Drag empty space | marquee select (Shift adds to the selection) |
| Shift-click a node | add it to or remove it from the selection |
| Ctrl-click a node (no drag) | toggle Quick Shift membership |
| Right-click | context menu |
| Delete / Backspace | delete the selected nodes |
| Ctrl+A | select all |
| Arrow keys (with a selection) | nudge x by one grid cell (Shift: ⅛ cell), y by 1 % (Shift: 0.1 %) |

**Line tool:** drag from A to B (§7.8). **Pencil tool:** drag to draw (§7.8). The tools are keys
in the toolbar; the Select tool is the default.

### 9.3 Global

| Keys | Result |
|---|---|
| Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y | undo / redo (UI edits: parameters, envelopes, presets loaded from the UI) |
| Ctrl+S | save the preset (opens Save as… for factory presets) |
| Esc | close a menu or overlay, cancel value entry, clear the selection |
| ↑ / ↓ in the preset browser | step, loading the preset. Enter loads and closes |

Undo history: 100 steps, UI-side only. A host automation change doesn't enter the stack.

---

## 10. Header, preset browser, shape library

### 10.1 Header preset bar

`[≡] | [‹]  Category / Preset name*  [›] | [☆] [⤓]`

* The name area opens the browser.
* ‹ › step within the browser's current filter, wrapping around.
* The star toggles favourite (outline `textMute`, filled `duck`).
* Save writes over a user preset, or asks Save as… for factory ones. "Saved" shows in `duck`
  for 1.25 s.
* Loading a preset morphs the curve (§11), resets `dirty` and is one undo step.
* `*` appears after any parameter or envelope change since load or save.

### 10.2 Preset browser (overlay)

**Panel.** Rect (248, 46, 828, 582), `ink2` fill, 1 px `ink6` border, radius 4, shadow per
§5.6. It covers the centre and right columns. The left column stays visible for context. A click
outside the panel on the chassis is swallowed (it doesn't reach controls underneath). Esc or ×
closes it.

**Search.** Well (264, 60, 300, 28) with a search glyph. The field has keyboard focus when the
browser opens; typing filters by name, category and mode.

**Filter list** (x 264–436, rows 24 px with a 26 px pitch):
* **All presets**, **Favourites** (star glyph), **User**, a divider, then the factory categories.
* Right-aligned counts (10.5 px `textDim`). Selected row: `ink4`, text `textHi` 620.

**Result list** (from x 468): columns ☆ | Name | Category (x + 300) | Mode (x + 440).
* Header 10.5 px `textDim`, rows 26 px.
* Hover `rgba(255,248,235,0.04)`.
* Loaded row: `ink4` with a 2 × 16 `duck` bar at the left.
* Star column click toggles favourite; row click loads; double-click loads and closes.

**Footer.** Hairline, "N presets · ↑/↓ to step · Enter to load" (`textDim`), **Save as…** and
**Open folder** keys at the right.

Factory content comes from the preset agent. The prototype's list is placeholder data.

### 10.3 Shape library (always visible)

**Tabs.** Sidechain · Rhythmic · Simple · User · ★ Favourites, 11.5 px/620.
* The active tab is `textHi` with a 2 px `duck` underline; others are `textDim` (hover
  `textMute`).
* At the right: page ‹ n / N › and "+ Save shape" (saves the current envelope to User and
  switches to it).

**Shape bank.** One well, 8 × 2 cells of 76.5 × 64, separated by hairline gutters
`rgba(255,248,235,0.045)` inset 6 px. A contact sheet, not cards.
* Each cell draws its shape in a 56.5 × 44 box: 1.5 px curve, fill @ 0.035.
* Idle: curve `textMute`. Hover: the curve eases to `textHi`, the cell gets a
  `rgba(255,248,235,0.04)` plate, and the main editor shows the dashed preview ghost.
* **Current** (the shape equals the edited envelope within 1e-3): `duck` curve and fill @ 0.10,
  plus a 2 px `duck` bar across the cell bottom.
* Favourite: a small `duck` star (scale 0.5) in the top-right corner.

**Gestures.** Click applies (one undo step + morph). Right-click opens the §6.12 menu. The
wheel pages.

---

## 11. Motion

Motion only answers an action or shows live signal. It is cheap enough to run at 60 Hz in
NanoVG.

| What | Duration and curve | Notes |
|---|---|---|
| Hover fades (key fill, text, arcs) | ~80 ms, exponential approach (rate 12/s) | per-control 0…1 value |
| Shape change (library, preset load, capture done, reset) | **180 ms**, cubic ease-out | lerp of both envelopes sampled at 97 points; nodes hidden during the morph, then the real nodes appear |
| Trigger flash (LED, editor bar, meter trough) | 1 → 0 linear over 200 ms | restarts on every `triggerCount` change |
| MIDI key / activity flash | 200 ms | |
| "Saved" | 1.25 s linear fade | |
| "Captured …" message | ~2.9 s linear fade | |
| Capture armed | display border alpha 0.55 ± 0.2·sin(t/220 ms); key dot blinks on a 180 ms rhythm | |
| Playhead, waveforms, meters | live, no easing | display smoothing per §7.3 and §8.4 |
| Menus, overlays, mode panels | instant | no slide or fade: it is a tool |

---

## 12. Prototype, screenshots and self-test

* **Open** `design/prototype/index.html` by double-clicking. Everything is embedded, via
  `assets/embedded.js`. The dev bar under the plugin has UI scale, host play/stop and a
  save-PNG button.
* **Screenshots:** `python tools/assets/screenshots.py` renders these states at 1×, and
  sync / spectral / multi / edit at 2×, into `design/prototype/screenshots/`:

  sync, midi, audio, spectral, ring, multi, edit, depth60, record, oneshot, presets, rate,
  entry, menu, hover, ms

* **Self-test:** `python tools/assets/selftest.py` drives the real event handlers with synthetic
  input. It covers 35 checks: knob drag / fine / reset / type-in, mode, split, node
  add / move / Ctrl-drag / Ctrl-click / bend / marquee / delete / arrow nudge, undo / redo,
  rate stepper and grid, library, quick shift, line (snapped and free), pencil, threshold,
  crossover, presets, Ctrl+S, capture arm / cancel / complete.
* **URL params:** `?scale=` (1–2), `?shot=<state>`, `?freeze=<phase>`, `?test=1`.

---

## 13. Phase 2 implementation plan (`src/ui/`, NanoVG / DGL)

### 13.1 Structure

The UI is a single top-level NanoVG surface with a light retained component tree. Components
paint in 1× units under one global scale, and input is routed by a hit list rebuilt on every
paint. That is the prototype's model, which proved simple for overlays, menus and capture.

```
plugin/KickarseUI.cpp          (build agent) → constructs kick::ui::Root, forwards DPF callbacks
src/ui/
  Theme.h                      colour/size/font tokens (this document §3–§4), constexpr
  Gfx.h/.cpp                   NanoVG helpers: well, raised, groove, lamp, dropShadow, dashed polyline,
                               snapped hairlines, text(role,…), measure, image draw, arc helpers
  Icons.h/.cpp                 icon(name, cx, cy, colour, scale): paths from §6.10
  Assets.h/.cpp                font + image loading from kick::res (§13.3)
  Model.h/.cpp                 UI-side mirror of params + envA/envB + uiState; setParam() with
                               begin/set/end gesture helpers; dirty flag; UndoStack (snapshots)
  Input.h/.cpp                 HitList (id, rect, handlers, cursor, hint), capture, double-click
                               timing (320 ms), modifiers, hover animation map
  widgets/
    Knob.cpp (small|hero)      LampButton.cpp   Segmented.cpp   Stepper.cpp   ValueField.cpp
    Dropdown.cpp  Menu.cpp  RateGrid.cpp  MsSlider.cpp  ValueEntry.cpp  IconButton.cpp
    ThresholdMeter.cpp  FilterGraph.cpp  CrossoverGraph.cpp  SpectrumView.cpp  Keyboard.cpp
    GrMeter.cpp  BarView.cpp
  panels/
    Header.cpp (wordmark, preset bar, undo/redo, settings, bypass)
    TimePanel.cpp   TriggerPanel.cpp (+ ModeSync/Midi/Audio/Spectral/Ring)
    EditorToolbar.cpp   EnvelopeEditor.cpp (+ QuickShiftLane, tool previews, readout)
    EditorFooter.cpp (band tabs, quick shift, rotate, capture)
    ShapeLibrary.cpp   AmountPanel.cpp (depth, bands, stereo, output)   Footer.cpp (hint, scale, grip)
    PresetBrowser.cpp (overlay)
  Root.h/.cpp                  owns everything: paint order, overlay stack, idle/animation, Bridge polling
  EnvelopeView.h/.cpp          drawing maths: PhaseMap mapping, segment sampling with Envelope::shape,
                               node/handle hit tests, morph sampling
```

Paint order is chassis → header → left → centre → right → footer → overlays (preset browser,
menus, value entry). Hit regions are appended in the same order, and hit tests walk the list
backwards, so overlays win.

### 13.2 Scaling and crispness

* `k = getWidth() / 1080.f`; the aspect ratio is locked. At the top of `onNanoDisplay`:
  `nvgScale(k, k)`. All code uses 1× units.
* UI scale choices call `setSize(1080·s·host, 660·s·host)`, where `host = getScaleFactor()`,
  and store `s` in `uiState`.
* `setGeometryConstraints(1080·host, 660·host, true)`: the minimum is 100 %.
* The grip calls `setSize` from the drag, with 5 % steps.
* **Hairlines.** `snap(v) = (floor(v·k) + 0.5) / k`, and stroke width `max(1, round(k)) / k`
  for 1 px rules (grid, grooves, playhead, separators). Everything else (curves, arcs) is
  anti-aliased at fractional widths as specified.
* **Text** is rasterised by fontstash at the transformed size, so it stays crisp at any k.
  Don't pre-scale font sizes.
* **Images:** the knob PNGs are @2× (mip-mapped: crisp at 200 %, clean at 100 %). The grain maps
  to device pixels (§5.1).

### 13.3 Assets and fonts

| Resource (kick::res name) | Type | Size | Load |
|---|---|---|---|
| `ArchivoSC-Medium` | font | 32 KB | `createFontFromMemory("sc", …)` |
| `ArchivoSC-SemiBold` | font | 32 KB | `createFontFromMemory("sc-semi", …)` |
| `ArchivoExp-SemiBold` | font | 32 KB | `createFontFromMemory("exp", …)` |
| `knob_hero@2x` | PNG 192² | 22 KB | `createImageFromMemory(…, IMAGE_GENERATE_MIPMAPS)` |
| `knob_small@2x` | PNG 88² | 6 KB | same |
| `grain_256` | PNG 256² | 80 KB | `IMAGE_REPEAT_X \| IMAGE_REPEAT_Y` |
| `logo_wordmark@2x` | PNG | 5 KB | not needed at runtime (installer / about); the wordmark is drawn from `logo_wordmark.json` compiled into `src/ui/Wordmark.inc` |

The total embedded is about 210 KB. `cmake/GenerateResources.cmake` (build agent) already globs
`resources/fonts/*.ttf` and `resources/images/*.png` (non-recursive, so `resources/fonts/src/`
is ignored).

### 13.4 Data flow and repaint

* `parameterChanged(i, v)` → Model (flags repaint).
* `stateChanged("envA" | "envB" | "uiState" | "presetName")` → Model.
* **UI edits:**
  * parameters: `editParameter(i, true)` / `setParameterValue(i, v)` / `editParameter(i, false)`
  * envelopes: `setState("envA", env.serialize())` on each committed change. Throttle during
    drags to at most 30 Hz, and always send on mouse-up.
* `uiIdle()` (about 60 Hz, DPF-driven) reads `Engine::bridge()` via
  `getPluginInstancePointer()`.
  * Repaint when:
    * the phase moved more than 0.5 px,
    * `triggerCount` or `recState` changed,
    * a spectral or wave frame is due (in Spectral mode, or when waveforms are shown and the
      host is playing),
    * any animation is running (hover, morph, flash), or
    * the model is dirty.
  * Otherwise skip painting, so an idle, stopped host costs almost nothing.
  * Copy the waveform and spectral arrays once per idle into UI-owned buffers (relaxed loads)
    and apply display smoothing there.
* `uiState`: a tiny key=value string (scale, libTab, libPage, show In/Side/Out, snap, qsOn,
  tool, hints, editBand). It is saved with the project.

### 13.5 Things NanoVG lacks (and the substitutes)

| Canvas feature used in the prototype | NanoVG equivalent |
|---|---|
| `setLineDash` | a helper that walks the polyline and emits dash segments (3/3 and 4/3) |
| rounded-rect `clip()` | avoided: inner shadows are gradient fills of the same path (§5.3); content inside wells is inset ≥ the radius and clipped with a rectangular `nvgScissor` |
| `roundRect` | `nvgRoundedRect` |
| `drawImage` | `nvgImagePattern` + `nvgRect` + `nvgFill` |
| `createPattern` with transform | `nvgImagePattern(0, 0, 256/k, 256/k, 0, img, a)` |
| radial and linear gradients | `nvgRadialGradient` / `nvgLinearGradient` |
| `boxGradient` shadow (prototype helper) | `nvgBoxGradient` (identical ramp) |
| `fill('nonzero')` for the wordmark | per-contour `nvgPathWinding(NVG_SOLID / NVG_HOLE)` from the JSON `hole` flags |
| `measureText` | `nvgTextBounds` |
| `textBaseline = 'middle'` | `NVG_ALIGN_MIDDLE` |

### 13.6 Order of work (Phase 2)

1. Theme, Gfx, Icons, Assets, and the Root skeleton with chassis + header + footer. Check the
   screenshots match at 1× and 2× in the standalone JACK/native build.
2. Model + Input + value widgets (knob, lamp, segmented, field, stepper, menus, entry) + Time and
   Amount panels.
3. EnvelopeEditor (drawing, then interactions, then Quick Shift, tools, readouts), wired to
   `setState`.
4. Mode panels, meters and graphs driven by the Bridge.
5. Shape library, preset browser (with the preset agent's API), capture flow.
6. Undo, uiState persistence, scaling polish, performance pass (repaint gating, no allocations
   in paint).

---

## 14. Asset pipeline (`tools/assets/`)

| Script | Output | Notes |
|---|---|---|
| `fonts.py` | `resources/fonts/Archivo*.ttf` | instancing + subsetting from `resources/fonts/src/Archivo[wdth,wght].ttf` (fonttools) |
| `textures.py` | `resources/images/grain_256.png` | seeded blue-ish signed noise |
| `knobs.py` | `resources/images/knob_hero@2x.png`, `knob_small@2x.png` | numpy PBR height-field renderer; deterministic |
| `logo.py` | `resources/images/logo_wordmark.{json,svg}`, `@2x.png` | glyph outlines → paths with the duck offsets |
| `embed_prototype.py` | `design/prototype/assets/embedded.js` | inlines fonts / PNGs / wordmark for `file://` use |
| `screenshots.py` | `design/prototype/screenshots/*.png` | headless Chrome/Edge with a private throw-away profile |
| `selftest.py` | console report | runs `index.html?test=1` headless; exit code 1 on failure |

Rebuild everything with:

```
python tools/assets/fonts.py
python tools/assets/textures.py
python tools/assets/knobs.py
python tools/assets/logo.py
python tools/assets/embed_prototype.py
python tools/assets/screenshots.py
python tools/assets/selftest.py
```

Needs Python 3.14 with numpy, pillow and fonttools.

---

## 15. Requests to the lead and DSP side, and open questions

### 15.1 Additive Bridge fields the UI wants

1. `std::atomic<float> scLevelDb`: sidechain detector level (after the detection filter), fast
   peak with about 300 ms release, for the threshold meter. Deriving it from `extWave` bins
   only works while the phase is moving.
2. `std::atomic<int> lastNote`, `std::atomic<int> lastVelocity`, `std::atomic<uint32_t>
   noteCount`: for the MIDI panel's activity, keyboard flash and **Learn**.
3. `std::atomic<int> timeSigNum`, `timeSigDen`: so the Sync bar view isn't hard-wired to 4/4.
4. Optional: `std::atomic<float> outPeakDb` for a future output meter, and a 128-sample
   `ringModScope[]` if we ever want to show the ring modulator.

### 15.2 Envelope contract notes (no change needed, recorded for the UI)

* `shape()` sign: t > 0 eases in. The UI maps "drag up = curve up" per segment direction (§7.4).
* `evaluate(1.0)` wraps to the first node, so the UI draws segments with `shape()` directly. An
  additive `float evaluateLeft(float p)` (the left limit at p = 1) would be a nicety, not a
  requirement.

### 15.3 Build agent

`plugin/DistrhoPluginInfo.h` currently says 1000 × 660. The design is **1080 × 660**. Please set
`DISTRHO_UI_DEFAULT_WIDTH 1080` and `setGeometryConstraints(1080, 660, true)` (minimum 100 %),
or tell me to adapt.

### 15.4 Open questions

1. Confirm the UI-only label changes:
   * Mode "Ring Mod" → **Ring**
   * trigSource "Continuous" → **Free**
   * timeMode "Sync" → **Note** (avoids two different "Sync"s)
   * Params.h labels stay as they are for hosts.
2. MIDI note naming: C3 = 60 (Ableton, Bitwig) or C5 = 60 (FL Studio)? The proposal is a setting
   with C3 = 60 as the default.
3. Reset gestures: we accept *both* double-click and Ctrl-click as reset, and use Alt-click or
   the value text for typing. OK?
4. Pop-up tooltips were replaced by the hint line. OK, or do we want both (off by default)?
5. Should Delta also change what the Out waveform shows (removed signal)? That needs no new
   Bridge data if the UI computes In − Out per bin.
6. Factory shape and preset names and categories (preset agent). The prototype's are
   placeholders.
7. What the bypass key should dim: only the curve and playhead (proposed), or the whole UI?

---

## 16. Phase 2: what shipped in `src/ui/` (implementation map)

| File | Role |
|---|---|
| `Theme.h` | tokens of §3 and the layout rects of §2.2 |
| `Gfx.h/.cpp` | NanoVG helpers: well, raised key, groove, lamp, drop shadow, snapped hairlines, text roles, images, dashed polylines, the §6.10 icon set |
| `Format.h/.cpp` | value formats (§4.3), normalised↔plain mapping, type-in parsing, note names |
| `Model.h/.cpp` | parameter cache and the begin/set/end plumbing, envelope mirrors, preset identity, `uiState`, History interface (local fallback) |
| `Widget.h`, `Controls.cpp` | widget base and value kit: knob (small/hero), value field, value text, lamp button, icon button, segmented |
| `Displays.h/.cpp` | rate stepper, grid dropdown, M/S slider, threshold meter, detection-filter graph, crossover graph, spectral view, bar view, note stepper, keyboard, GR meter |
| `EditorView.h/.cpp` | the envelope editor view (§7), drawing from an `EditorBackend` |
| `EditorGlue.h/.cpp` | backend over the headless `src/editor` (EditorModel + EditorController); also the undo History and the envelope → host listener |
| `EditorLocal.h/.cpp` | display-only backend (fallback) |
| `Library.h/.cpp` | shape library: tabs from `kShapeCategories` + Favourites, pager, Save shape, shape bank |
| `PresetBrowser.h/.cpp` | overlay over `PresetStore`: search, filters from `categories()`, favourites, Save as…, Open folder |
| `Overlays.h/.cpp` | list menus and the rate grid |
| `View.h/.cpp` | root: layout of every panel, input routing, overlays, Bridge polling with display ballistics, presets/shapes/capture plumbing, value/text entry |
| `Settings.h/.cpp` | `Documents\Kickarse\settings.txt`: UI size, MIDI note naming, tooltips, animation, shape favourites |
| `DemoFeed.h/.cpp` | synthetic Bridge data for design review (`KICKARSE_UI_DEMO=1`) |
| `WordmarkData.h` | generated by `tools/assets/wordmark_header.py` |
| `SelfTest.cpp` | in-plugin interaction test (`KICKARSE_UI_SELFTEST=<file>`, 28 checks through the real input path) |

`plugin/KickarseUI.cpp` is a thin DPF adapter.
* Events arrive in window pixels and are divided by `k = width / 1080`.
* The size is 1080×660 × host DPI factor × user scale.
* It includes the snapshot hook: `KICKARSE_UI_SNAPSHOT`, `KICKARSE_UI_SHOT`, `KICKARSE_UI_SCALE`, `KICKARSE_UI_SNAPSHOT_EXIT`.

**Verification tooling.**
* The standalone needs a 4-input audio device, so `tools/assets/uihost/` builds a minimal VST2 editor host. It opens the real plugin, feeds a kick + bass, and pumps idle.
* `python tools/assets/ui_snapshots.py` captures every state at 1× (and four at 2×) into `design/prototype/screenshots/native/`.
