# Kickarse — Design research

Phase 1 input for `docs/design/DESIGN.md`. Four questions:

1. What gives away an interface as AI-generated, and how does Kickarse avoid each tell?
2. What do the best plugin UIs do that the rest don't?
3. How much 3D and material should a plugin have, and how do you make it look expensive
   instead of cheap?
4. Which interaction conventions are users' muscle memory, so we must not break them?

Everything is paraphrased; sources are linked at the end of each section. "Observation" marks
things we saw ourselves (in the plugins or in the reference screenshot) rather than read.

---

## 1. The tells of AI-generated interfaces

### 1.1 Why they happen

Several write-ups give the same mechanism. A model samples the most probable design, and
the web's most probable design is a template: Tailwind's `indigo-500` default spread through
tutorials and GitHub, so "modern" came to mean purple; Inter became every starter kit's
typeface; shadcn and bento layouts made identical cards the norm. Anthropic calls the effect
"distributional convergence". Its fix is to decide the aesthetic on purpose (type, colour,
motion, composition) and to be bold in one place only. The same pattern now shows up in audio.
KVR members say they suspect a plugin was vibe-coded when they see the same small type in the
same large boxes, the same knobs, the same palette in every release and a generic dark website.

### 1.2 The do-not list

| # | Tell | Why it reads as generated | Kickarse rule (how we avoid it) |
|---|------|---------------------------|---------------------------------|
| 1 | Purple→blue / indigo gradients, "VibeCode purple" | the Tailwind default, the loudest tell of 2025–26 | No purple or indigo anywhere. The palette comes from the product: the envelope ("duck") colour is a warm rubber-duck yellow `#FFB81C` (ducking, get it); the sidechain/kick is signal red; highs are sky blue. No gradients as decoration. Gradients appear only as *lighting* (a top-lit chassis) or as *data* (a fill fading under a curve). |
| 2 | Inter / Geist / Poppins / Space Grotesk / DM Sans / Manrope | the reflex fonts that no one chose | **Archivo** (Omnibus-Type, OFL) in three widths: semi-condensed for labels, expanded for hero numbers. The wordmark is the family's expanded weight turned into paths. A fifth-rank grotesque with a width axis: chosen for density, not trend. |
| 3 | Glassmorphism, frosted panels, backdrop blur | trend styling that also kills contrast | None. NanoVG has no blur anyway. Overlays are solid panels with a real drop shadow. |
| 4 | Neon glows on everything, coloured box-shadows | "premium" faked with bloom | Glow is used only where real light would glow: LED lamps and a hovered node. Shadows are black, low alpha, from one light. |
| 5 | Identical rounded cards, three-in-a-row, bento grids | the template's layout primitive | **No cards.** One continuous machined panel, divided by grooves (a dark line plus a light line). Recessed *wells* only where something is displayed. The shape library is a single well split by hairlines (a contact sheet), not 16 floating tiles. |
| 6 | Uniform 8–16 px radius on everything | no one decided the radius | A small, deliberate scale: 3 px on wells and keys, 2 px on inner keys and cells, 4 px on popovers, round only for knobs. Precision instruments have tight radii. |
| 7 | Emoji or Lucide-icon soup | icons as decoration, from a stock set | About 30 custom icons on a 14 px grid with 1.5 px strokes. Text labels wherever a word is clearer than a glyph ("Snap", "Split", "Delta", "Capture"). |
| 8 | Low-contrast grey-on-grey body text | dark mode without checking | Measured tokens (§3.6 of DESIGN.md). Labels 5.6:1, values 10.8:1, tertiary text 3.9:1, which is used only for help copy that isn't needed to operate anything. |
| 9 | Centred everything, hero badge, "stat rows" | web landing-page reflexes | Left-aligned, signal-flow reading order: Time/Trigger (left) → Shape (centre) → Amount/Bands (right) → hint line (bottom). |
| 10 | Gradient text, accent word in italic serif | decoration in type | The type is always a single colour. The one typographic gesture is the wordmark, where the letters of "arse" duck and recover like the signal. It *is* the product idea, not ornament. |
| 11 | All-caps tracked labels everywhere | hardware cosplay; also listed as an AI tell | Sentence-case labels ("Low mix", "Quick shift"). Mixed case reads faster at 11 px. No caps anywhere except note names. |
| 12 | Decorative blobs, radial orbs, dot grids, sparkles | filler | Every mark on the canvas is data or a control. The only texture is a 16 %-opacity device-pixel grain that dithers gradients. |
| 13 | Bounce and hover animation on every element | "micro-interactions" sprinkled everywhere | Motion answers an action: shape morph 180 ms, hover fades 80 ms, trigger flash 200 ms, capture arming pulse. Nothing idles or bounces. |
| 14 | Permanent near-black with one acid accent | now its own cluster of defaults | Warm graphite chassis (`#1F1E1C`, L≈12 %), not black. Three semantic hues with fixed meanings instead of one "brand accent". |
| 15 | Missing states (focus, empty, error) | generated code ships only the happy path | Every component in DESIGN.md has idle / hover / active / disabled / focus. There are empty states (quick shift with no group, a trigger that never arrives, a capture that is still armed). |
| 16 | iOS pill toggles | a web/mobile component in a plugin; the reference uses them too | **Lamp buttons**: a raised key with a square LED that lights up in the parameter's colour. They're compact, readable, and on/off shows in both the lamp and the text brightness. |

Sources: [Developers Digest, 16 patterns](https://www.developersdigest.tech/blog/ai-design-slop-and-how-to-spot-it) ·
[925 Studios, AI slop tells](https://www.925studios.co/blog/ai-slop-design-tells) ·
[SmoothUI, AI design slop](https://smoothui.dev/blog/ai-design-slop) ·
[prg.sh, why AI builds purple gradients](https://prg.sh/ramblings/Why-Your-AI-Keeps-Building-the-Same-Purple-Gradient-Website) ·
[GitHub checklist, 30 signs](https://github.com/ChinmayBhattt/AI-Generated-Website) ·
[Anthropic, improving frontend design through Skills](https://claude.com/blog/improving-frontend-design-through-skills) ·
[Anthropic frontend-design skill](https://github.com/anthropics/skills/blob/main/skills/frontend-design/SKILL.md) ·
[Bruvora, AI-slop typography](https://www.bruvora.com/blog/stop-ai-slop-typography) ·
[Sailop, Geist is the new Inter](https://sailop.com/blog/geist-the-new-inter-ai-font-fingerprint-2026) ·
[RAXXO, dark mode that doesn't look AI](https://raxxo.shop/blogs/lab/dark-mode-design-that-doesnt-look-ai) ·
[KVR, vibe-coded plugins thread](https://www.kvraudio.com/forum/viewtopic.php?t=627485&start=75) ·
[Voger Design, glassmorphism in plugin UI](https://vogerdesign.com/blog/glassmorphism-audio-plugin-ui/)

---

## 2. What best-in-class plugin UIs do

### 2.1 Patterns across the field

**FabFilter (Pro-Q 4, Pro-C 2, Volcano).** The display is the interface. It fills the window and
you edit the curve directly: double-click or drag to create a band, drag a rectangle to select
several, Shift for fine moves, Alt to constrain an axis, the wheel for the secondary dimension,
right-click for a menu. Controls float near the selection instead of sitting in a fixed rack.
Knobs follow one convention across the range: vertical drag that is speed-sensitive, Shift for
fine, wheel on hover, Ctrl-click to reset, double-click to type a value. Reviewers keep praising
the same thing: every feature the job needs, nothing more, and every control shaped for a mouse.
*Takeaway:* the envelope editor is our FabFilter display, and it should be the largest, calmest
and most direct thing on screen.

**Kilohearts.** Flat, consistent modules. Colour is information: control-rate modulation is
orange, audio-rate green, modulation-of-modulation yellow. The developers say the UI usually
takes a long time whenever it does something special. *Takeaway:* give each hue one meaning and keep it everywhere. Kickarse uses
yellow for the duck (envelope, depth, cut), red for the sidechain (kick waveform, trigger LED,
threshold meter, capture) and blue for the high band.

**Cableguys ShaperBox / VolumeShaper.** This is the closest relative to our editor. Points come
in three weights (hard, medium, soft). You drag on a line to create a curve, click a point to
toggle line and curve, double-click to delete, Shift-click to soften, and hold Shift to snap
temporarily. A marquee selection gets a bounding box you can move, scale and skew. Alt locks
vertical movement. Line, arc and S-curve pens draw and repeat shapes. An oscilloscope behind
the curve shows input in grey and the processed signal in colour, and a "modulation trace" shows
the smoothed result when Smooth is high. The manual suggests nudging a sidechain curve slightly
earlier or later to tighten the groove, which is exactly what our Quick Shift does.
*Takeaways:* "drag the segment to bend it" is learned behaviour, so we adopt it. Grey input plus
coloured output is readable, so we adopt the principle (input outline, output fill, sidechain in
red). Draw the curve that is actually applied when it differs from the drawn one: our dashed
"effective curve" at Depth < 100 %.

**Xfer LFO Tool / Serum.** You drag points freely, double-click to add or remove, and drag a
segment's handle to bend it. Alt-drag snaps to the grid, and Alt on a tension handle bends all of
them. The grid, the snap and the swing are musical first. *Takeaway:* a shape library and grid
presets make the tool fast. We keep the library permanently visible under the editor.

**Tokyo Dawn Labs.** Dense, text-rich and utilitarian, with a user-selectable UI scale
(100/125/150 %). Proof that density works if hierarchy and alignment are strict.
*Takeaway:* UI scale lives in a menu *and* in a drag corner, and every size stays crisp.

**u-he.** The Zebra 3 generation moved to a vector UI with less clutter and colour-coded modules,
after years of bitmap skins. *Takeaway:* vector-first for everything that moves or scales.

**Soundtoys.** Hardware character is the brand: one hero control (Decapitator's Drive) and style
buttons. A user review of EchoBoy complains that knob values are hard to read at a glance
without good eyesight or a big screen. *Takeaway:* character is fine, but values must always be
legible. Every Kickarse knob prints its value, in tabular figures.

**Goodhertz.** No knobs at all; the team argues knobs are a poor way to show a number on a
screen. Custom-set type (their own Coldtype tool) and variable-width fonts, confident colour.
*Takeaway:* typography can carry identity. We keep knobs where rotation helps (bipolar, "amount"
controls) and use number fields for Swing, Smooth, Rotate and Length.

**oeksound soothe2.** One big spectral display that shows the reduction directly, with a few
large controls. *Takeaway:* our Spectral panel shows **the cut per band** as the primary mark
(yellow, hanging from the top) with the sidechain spectrum as secondary context (a red line).

**Baby Audio.** Bold colour and 3D product styling with a clear brand identity. Proof that a
plugin can be playful and still sell as professional. *Takeaway:* the cheek goes in the brand
(wordmark, microcopy), not in the controls.

**Minimal Audio (Current).** Reviews praise the uncluttered layout and fast drag-and-drop
modulation. One review finds the grey-and-purple scheme a little flat. *Takeaway:* a
monochrome-plus-one-accent palette reads as flat. Ours uses three semantic hues on a lit, grained
graphite.

**Output (Portal, Arcade).** Observation: moody full-bleed atmosphere and big macro controls,
with complexity hidden behind a "flip" view. *Takeaway:* progressive disclosure. Kickarse shows
only the active mode's controls, dims the multiband section when Split is off, and shows band
tabs only when there are two envelopes.

**Arturia (Pigments vs the V Collection).** Pigments is praised because its colour-coded
modulation makes complex routing readable. KVR users call Arturia's hardware-replica UIs
ridiculously skeuomorphic. *Takeaway:* colour-as-routing is good; photo-real hardware is out of
fashion with the very people who buy sidechain tools.

### 2.2 Hierarchy, density, small-size readability

* One focal element per screen (FabFilter's curve, soothe's spectrum, Decapitator's Drive).
  Kickarse has two, deliberately ranked: the Depth knob (the only light-coloured object) and the
  envelope (the only large saturated line). The squint test in DESIGN.md §2.3 checks the ranking.
* Density comes from alignment, not smaller type. The text floor is 10 px for axis labels and
  11 px for labels. Values are one step heavier than labels.
* Labels sit next to their values. KVR users single out "small fonts in big boxes" as a
  vibe-coded tell.

### 2.3 How curve and envelope editors handle nodes, curvature and selection

| Concern | Common solutions | Kickarse |
|---|---|---|
| Create node | double-click (LFO Tool, Pro-Q), click on line (ShaperBox) | double-click empty space, snapped |
| Delete node | double-click node (LFO Tool, ShaperBox), right-click menu | double-click node; Delete key for a selection; menu |
| Curvature | drag a mid-segment handle (LFO Tool/Serum), drag the line itself (ShaperBox), point weights (ShaperBox) | drag the segment *or* its handle vertically; handles are solid dots so they never read as nodes; double-click a handle to straighten |
| Selection | marquee + Shift-add (Pro-Q, ShaperBox) | marquee, Shift-click to add, Ctrl+A |
| Constrain / fine | Alt = axis lock, Shift = fine (Pro-Q) | same |
| Snap | toggle plus a temporary override key (ShaperBox: Shift) | toggle plus Ctrl held while dragging to invert (Shift is already "fine") |
| Group move | selection box drag (ShaperBox) | selection drag, plus the dedicated **Quick Shift** lane for the "dip + recovery" group |
| Readout while dragging | tooltip with value (Pro-Q) | tag next to the cursor: time in ms and gain in dB *at the current Depth* |

### 2.4 Preset browsers

Established patterns: a search field that takes focus when the browser opens, a filter list
(categories, User, Favourites), a star toggle per row, arrow keys to step, Enter to load, Esc to
close, prev/next arrows in the header that step within the current filter, and a dirty marker.
Kickarse follows all of them. The browser is an overlay inside the plugin window over the editor
and Amount columns, so the Time/Trigger column stays visible for context. It never opens a
separate OS window.

Sources: [FabFilter Pro-Q 4 overview](https://www.fabfilter.com/help/pro-q/using/overview) ·
[Pro-Q 4 display & workflow](https://www.fabfilter.com/help/pro-q/using/eqdisplay) ·
[FabFilter knobs](https://www.fabfilter.com/help/pro-q/using/knobs) ·
[Sound On Sound, Pro-Q 4](https://www.soundonsound.com/reviews/fabfilter-pro-q-4) ·
[Kilohearts modulation docs](https://kilohearts.com/docs/modulation) ·
[Admiral Bumblebee, Kilohearts interview](https://www.admiralbumblebee.com/music/2019/07/13/Interview-with-Kilohearts.html) ·
[ShaperBox 3 manual](https://downloads.cableguys.com/Cableguys-ShaperBox-3-Manual.pdf) ·
[Xfer LFO Tool](https://xferrecords.com/products/lfo-tool) ·
[FaderPro, how to use LFO Tool](https://blog.faderpro.com/plugins/how-to-use-xfer-records-lfo-tool/) ·
[Serum manual](https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf) ·
[TDR Kotelnikov GE manual](https://docs.tokyodawn.net/kotelnikov-ge-manual/) ·
[Synth Anatomy, Zebra 3](https://synthanatomy.com/2026/07/u-he-zebra-3-modular-synthesizer-plugin.html) ·
[Drumspy, Soundtoys review](https://drumspy.com/soundtoys-review) ·
[Sound On Sound, Goodhertz](https://www.soundonsound.com/reviews/goodhertz-plug-ins) ·
[Goodhertz, a note on the type](https://goodhertz.com/type-note/) ·
[OH no Type, Rob Stenson interview](https://ohnotype.co/blog/rob-stenson-interview) ·
[MusicTech, soothe2](https://musictech.com/reviews/plug-ins/oeksound-soothe2/) ·
[Behance, Baby Audio identity](https://www.behance.net/gallery/159747765/Baby-Audio) ·
[MusicRadar, Current 2.0](https://www.musicradar.com/music-tech/soft-synths/minimal-audio-current-2-0-review) ·
[Sound On Sound, Pigments](https://www.soundonsound.com/reviews/arturia-pigments) ·
[KVR, best and worst GUIs](https://www.kvraudio.com/forum/viewtopic.php?t=625585) ·
[Native Instruments, browser & presets](https://docs.native-instruments.com/ni-tech-manuals/komplete-kontrol-manual/en/browser-and-presets) ·
[Voger Design, UX patterns in synth plugins](https://vogerdesign.com/blog/basic-ux-patterns-in-the-development-of-synthesizer-plugins-awesome/) ·
[Massey University, *Knobs and Nodes*](https://mro.massey.ac.nz/items/f630372b-5871-4864-b4ab-aefc49e4e1fc)

---

## 3. 3D and material in audio UIs

### 3.1 What the practitioners do

* Pro knob art is modelled and rendered: Blender, Cinema 4D, KeyShot, sometimes CAD in
  Fusion 360. Each step is rendered to a filmstrip of roughly 64–128 frames, about 250 px per
  frame, and touched up in Photoshop.
* Lighting is the craft. Three-point lighting isn't enough; serious renders use many soft area
  lights plus an HDRI, often warm and cool fills on either side for depth.
* The classic mistake: when a real knob turns, the metal turns but the ceiling light doesn't.
  Highlights, shadows and ambient occlusion must stay fixed while only the texture and the
  pointer rotate. Forum advice: render the body and the highlights as separate passes, or bake
  them together in 3D so the specular stays put.
* Filmstrips are expensive. A 128×128 px, 128-frame strip is about 8 MB uncompressed in memory,
  and they don't scale: every UI size needs its own strip or gets blurry.

### 3.2 Lighting model vocabulary (as used in DESIGN.md)

* **One key light**, upper left and slightly above the panel, shared by every raster and vector
  element: highlights on top edges, shadows falling down and a little right. Material's
  environment model adds an ambient light for the soft all-round shadow and contact darkening.
  Comeau's rule: every shadow keeps the same offset ratio and grows softer and lighter with
  elevation, and shadows take on the surface hue instead of neutral grey.
* **Diffuse vs specular.** Diffuse (Lambert) gives the shape's form. Specular (GGX microfacets)
  gives the material: sharp and small on glaze, broad and dim on rubber, stretched on brushed
  metal.
* **Anisotropy.** On concentrically spun metal (the classic knob cap), grooves run around the
  circle and highlights stretch radially, perpendicular to the grooves: the "CD" look KeyShot
  calls radial anisotropy.
* **Ambient occlusion and contact shadow.** Darkening where surfaces meet (knob foot to panel,
  cap to collar) is what makes an object sit *on* the panel instead of floating.

### 3.3 Cheap vs premium skeuomorphism

Cheap: inconsistent light directions, textures that exist for their own sake (leather, wood,
brushed-metal wallpaper), heavy bevels, and bitmaps that blur when scaled. It also dates fast:
KVR users call Arturia's replicas ridiculously skeuomorphic and a 20-year-old synth look dated.
Neumorphism's soft same-colour shadows fail contrast checks for controls.

Premium: restraint. Texture is used only when it says something. Execution is technically sound
(one light, correct highlights, crisp at every size). Hardware panels can be convincing when
they are honest about what they are.

### 3.4 Decision: how much 3D Kickarse has, and why

**One physical object on a precise, flat instrument.**

* The **Depth** knob is the only rendered "hero": a glazed bone-ceramic cap on a spun gunmetal
  collar. It's the most-used control and the brand's memorable object, since a single warm-white
  knob on graphite reads at thumbnail size. That is where we spend the boldness.
* Small knobs share the same renderer, light and gunmetal, but in dark soft-touch rubber, so
  they recede.
* Everything that moves, carries a value, changes colour or has to be crisp at 100–200 % is
  vector: rings, pointers, the curve, meters, text and icons.
* **No filmstrips.** The bodies are rotationally symmetric, and the light doesn't move when a
  real knob turns, so one static, lit PNG per size is physically correct. Only the vector
  pointer rotates. That's 22 KB + 6 KB instead of megabytes, and it scales cleanly: the PNGs are
  rendered at 2× and mip-mapped down.
* The panel gets a *hint* of material: a top-lit gradient, a 16 %-opacity device-pixel grain,
  machined grooves and recessed wells whose inner shadow comes from the same key light. No
  bevel frames, no screws, no fake leather.

The renderer (`tools/assets/knobs.py`) is a small physically based height-field shader: analytic
profile SDFs for a surface of revolution, isotropic and anisotropic GGX, Schlick Fresnel, an
analytic softbox environment, horizon-based AO, a soft ray-marched shadow, 4×4 supersampling
and ACES-fit tone mapping.

Sources: [HISE, Blender knob filmstrips](https://forum.hise.audio/topic/2986/using-blender-for-knob-filmstrip) ·
[HISE, 3D knobs](https://forum.hise.audio/topic/2149/3d-knobs/24) ·
[HISE, filmstrip size limit](https://forum.hise.audio/topic/1806/filmstrip-size-limit) ·
[KVR, knob specular highlight & animation](https://www.kvraudio.com/forum/viewtopic.php?t=452916) ·
[KVR, UI design tutorials](https://www.kvraudio.com/forum/viewtopic.php?t=541318) ·
[KeyShot, anisotropic material](https://manual.keyshot.com/manual/materials/material-types/advanced-material/anisotropic/) ·
[Material Design, light & shadows](https://m2.material.io/design/environment/light-shadows.html) ·
[Josh W. Comeau, designing shadows](https://www.joshwcomeau.com/css/designing-shadows/) ·
[NN/g, skeuomorphism](https://www.nngroup.com/articles/skeuomorphism/) ·
[Justinmind, skeuomorphic design](https://www.justinmind.com/ui-design/skeuomorphic) ·
[LANDR, skeuomorphism in plugins](https://blog.landr.com/skeuomorphism-plugins/) ·
[Tim Graf, glassmorphism vs neumorphism](https://timgraf.com/ui/glassmorphism-vs-neumorphism-high-end-ui-guide-2026/)

---

## 4. Plugin UX conventions (muscle memory)

### 4.1 Knobs and value controls

The field is split into two camps:

| Action | FabFilter / Serum camp | Bitwig / Kilohearts / Cableguys camp | **Kickarse** |
|---|---|---|---|
| Adjust | vertical drag, speed-sensitive | vertical drag | vertical drag, 200 px = full range (260 px for the hero) |
| Fine | Shift | Shift | Shift (×0.1), also for the wheel |
| Reset | Ctrl/Cmd-click | double-click | **both** double-click and Ctrl-click reset |
| Type a value | double-click | Ctrl-click (Bitwig) | **Alt-click**, or click the printed value, or right-click → Type a value… |
| Wheel | adjust on hover | adjust | 2 % per notch, 0.2 % with Shift |

Rationale: resetting is the frequent action and both camps' reset gestures are harmless, so we
accept both. Typing gets its own unambiguous gestures, and the value text itself is the most
discoverable one. Every control has a right-click menu (Reset, Type, Copy, Paste). Bitwig
applies its own conventions to third-party plugins in its device panel, which is another reason
not to fight the double-click reset.

### 4.2 Readouts, tooltips, help

* Values are always printed. On hover the value underlines to show it can be clicked to type.
* Instead of pop-up tooltips over the controls, a **hint line** in the footer shows the hovered
  control's name, value and gestures (the Ableton Info View / Bitwig status bar pattern). It
  never covers the thing you're looking at, and it can be turned off in the menu.
* Dragging a node or segment shows a small tag at the cursor: ms, dB, or curve amount.

### 4.3 Focus, keyboard, states

Plugins get keyboard focus unreliably (the host often keeps it). So shortcuts are conveniences
(Ctrl+Z/Y, Delete, Ctrl+A, arrows, Enter, Esc), never the only way to do something. There is no
Tab-order focus traversal because hosts own Tab. The editor takes the keyboard after a click,
and the value-entry field is the only element drawn as focused.

### 4.4 HiDPI and resizing

Hosts report a scale factor (VST3 and CLAP both have one; DPF exposes it as
`getScaleFactor()`/`uiScaleFactorChanged`). Bitmaps upscale blurry and some hosts (Max 8, for
one) mishandle high-DPI scaling on Windows, which is why many JUCE developers cache images per scale or go
vector. Kickarse draws everything in 1× units under one `nvgScale(s, s)`. It sets text with
NanoVG's scale-aware font atlas, uses bitmaps only at 2× and mip-mapped, and maps the grain
texture to device pixels. UI scale is user-selectable (100–200 %), with a resize grip that keeps
the aspect ratio.

### 4.5 Colour-blind safety and contrast

* The Okabe–Ito set shows that orange/yellow vs blue is the most robust pair across all common
  colour-vision deficiencies. Our three hues (yellow `#FFB81C`, sky `#4DB2F5`, red `#FF4A3D`)
  stay at least 16 OKLab ΔE units apart under simulated protanopia, deuteranopia and tritanopia
  (Machado et al. 2009, severity 1.0), where about 2 units is a just-noticeable difference.
  Where hue alone would be ambiguous there's a second cue: the high band gets its own tab
  label, the sidechain is always a line or trace while the envelope is a curve with nodes, and
  Quick Shift members are diamonds, not circles.
* WCAG 2.x asks for 4.5:1 for normal text and 3:1 for UI components. All labels and values
  pass (DESIGN.md §3.6). Tertiary help text is 3.9:1 and is never needed to operate the plugin.

Sources: [FabFilter knobs](https://www.fabfilter.com/help/pro-q/using/knobs) ·
[Bitwig user guide, interfacing](https://www.bitwig.com/userguide/latest/user_interfacing/) ·
[KVR, Bitwig double-click reset on VSTs](https://www.kvraudio.com/forum/viewtopic.php?t=569376) ·
[JUCE forum, scaling plugin image assets](https://forum.juce.com/t/scaling-plugin-image-assets/46816) ·
[JUCE forum, high-DPI support how-to](https://forum.juce.com/t/high-dpi-support-how-to/26141) ·
[JUCE forum, Max 8 Windows high-DPI](https://forum.juce.com/t/max-8-windows-high-dpi-issue/56670) ·
[Okabe–Ito palette (see package docs)](https://easystats.github.io/see/reference/scale_color_okabeito.html) ·
[Datawrapper, colour-blind readers](https://www.datawrapper.de/blog/colorblindness-part2) ·
[W3C, Understanding contrast (minimum)](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html) ·
[W3C, Understanding non-text contrast](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html) ·
Machado, Oliveira & Fernandes, "A Physiologically-based Model for Simulation of Color Vision
Deficiency", IEEE TVCG 15(6), 2009.

---

## 5. What the reference screenshot taught us (functional only)

Observation. The old plugin's information architecture:

* **Header:** logo, preset bar, undo/redo.
* **Left column:** crossover graph, MULTI toggle with band solo, LO/HI MIX with a link, DEPTH,
  rate stepper, loop/sync.
* **Main area:** toolbars (TOOLS, OPTIONS, WAVEFORMS, PROCESSING), the editor with grid labels,
  SHIFT and ROTATE.
* **Bottom:** a shape library with category tabs.

What we kept: every function, the always-visible shape library, the stepper for rate.

What we changed, and why:

1. Signal-flow columns: **When** (Time, Trigger) → **Shape** → **How much / where** (Depth,
   Bands, Stereo, Output). The old layout put Mid/Side in the editor's toolbar and the crossover
   graph above the depth.
2. The mode (Sync/MIDI/Audio/Spectral/Ring) is a first-class segmented control with a
   mode-specific panel. The old plugin had no modes.
3. Rate is a stepper *plus* a note grid (rows straight / dotted / triplet), instead of a
   one-dimensional list.
4. Threshold is a live meter with a draggable line, not a blind knob.
5. Envelope link lives with the band tabs *on the editor*, where it matters. Lo/Hi Mix stay
   in Bands.
6. Hovering a library shape previews it as a ghost on the editor before you commit.
7. At Depth < 100 %, the curve that is actually applied is drawn dashed.
8. Lamp buttons instead of iOS switches, sentence case instead of tracked caps, a warm graphite
   palette with three semantic hues instead of slate blue with coral and periwinkle.
9. Waveforms are a magnitude view on the gain axis, so the output silhouette takes the shape of
   the curve.
