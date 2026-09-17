# UI Specification v1 — Victor R Swag Vocal Compressor

**Project:** `VictorRSwagVocalComp`
**Framework:** WebView (WKWebView on macOS)
**Phase:** Design
**Source contracts:** `.ideas/creative-brief.md`, `.ideas/parameter-spec.md`, `.ideas/architecture.md`

---

## Layout

- **Window:** 640 × 420 px (fixed, non-resizable in v1)
- **Outer padding:** 24 px
- **Content width:** 592 px
- **Sections:** 4 stacked rows

```
┌────────────────────────────────────────────────────────────────┐ 640
│  VRS VOCAL COMP                         [ Pop Lead        ▾ ]  │  48   header
├────────────────────────────────────────────────────────────────┤
│                                                                │
│      ( CHARACTER )        (( AMOUNT ))        ( MIX )          │ 196   hero
│          90px                 150px             90px           │
│                                                                │
├────────────────────────────────────────────────────────────────┤
│  GR  ████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   -4.2 dB     │  44   meter
├────────────────────────────────────────────────────────────────┤
│   (SC HPF)      (OUTPUT)                        [  BYPASS  ]   │  60   utility
└────────────────────────────────────────────────────────────────┘
```

**Vertical budget:** 24 pad + 48 header + 16 gap + 196 hero + 12 gap + 44 meter + 12 gap + 60 utility + 8 pad = 420

### Row 1 — Header (48 px)
- **Left:** `VRS VOCAL COMP` wordmark, letter-spaced uppercase, muted until hover
- **Right:** preset dropdown, 180 px wide, six factory presets from `parameter-spec.md`
  (Init, Intimate Vocal, Pop Lead, Rap Upfront, Parallel Crush, Broadcast Even)
- 1 px bottom hairline in `--line`

### Row 2 — Hero (196 px)
Three rotary controls, centered as a group, baseline-aligned labels.

- **AMOUNT** — 150 px, center, the visual hero. Thicker arc stroke (7 px),
  accent glow that intensifies with value. This is the only control with a glow.
- **CHARACTER** — 90 px, left. Arc stroke 5 px. Sub-label under the value
  reads `SMOOTH` / `BALANCED` / `AGGRESSIVE` depending on zone (<33 / 33–66 / >66).
- **MIX** — 90 px, right. Arc stroke 5 px.

### Row 3 — Gain reduction meter (44 px)
- Full content width, HTML5 Canvas
- Default: **single summed bar**, 0 → −20 dB, filling left to right
- **On hover:** crossfades into **two stacked bars** — `OPTO` (top) and `FET`
  (bottom), 14 px each with a 4 px gap — revealing which stage is doing the work
- Numeric readout right-aligned, `-0.0 dB`, tabular figures
- Tick marks at −3, −6, −12, −20 dB
- Ballistics: instant attack, ~180 ms release decay on the visual peak

### Row 4 — Utility (60 px)
- **SC HPF** — 56 px mini knob, left. Value in Hz, logarithmic taper.
- **OUTPUT** — 56 px mini knob, next to it. Value in dB, bipolar arc drawn
  from 12 o'clock (center-zero).
- **BYPASS** — right-aligned pill button. Inactive = outlined muted; active =
  filled accent with the whole UI dimming to 45% opacity.

---

## Controls

| Parameter ID | Type | Position | Range | Default | Unit |
|---|---|---|---|---|---|
| `amount` | Rotary, 150 px | Hero, center | 0 – 100 | 35 | % |
| `character` | Rotary, 90 px | Hero, left | 0 – 100 | 50 | % |
| `mix` | Rotary, 90 px | Hero, right | 0 – 100 | 100 | % |
| `sc_hpf` | Rotary, 56 px | Utility, left | 20 – 300 | 85 | Hz |
| `output` | Rotary, 56 px (bipolar) | Utility, left-center | −12 – +12 | 0 | dB |
| `bypass` | Pill toggle | Utility, right | off / on | off | — |
| *(readout)* `gr_meter` | Canvas bar | Meter row | 0 → −20 | — | dB |

**Element IDs match parameter IDs exactly** — `amount-knob`, `amount-value`,
`character-knob`, `mix-knob`, `sc_hpf-knob`, `output-knob`, `bypass-btn`,
`gr-canvas`. The impl phase binds these to APVTS via `window.Juce` state objects.

### Interaction model
- **Vertical drag** to change value; 200 px of travel = full range
- **Shift + drag** = fine mode (÷5 sensitivity)
- **Double-click** = reset to default
- **Mouse wheel** = ±1 % step (±0.5 % with Shift)
- Pointer capture on drag so the cursor can leave the knob bounds
- Knob arc sweeps **270°**, from −135° (bottom-left) to +135° (bottom-right)

---

## Color Palette

- Background: `#0B0D10`
- Panel / surface: `#12161B`
- Primary (text): `#E6EDF3`
- Accent: `#00F5FF`
- Accent dim: `#0A7F88`
- Muted text: `#7D8894`
- Line / track: `#232A32`
- Warning (heavy GR, > −12 dB): `#FF4D6D`

---

## Style Notes

- **Flat, no skeuomorphism.** No metal, no screws, no bevels, no drop shadows
  imitating physical depth. Depth comes only from the accent glow.
- **One accent color does all the work.** Cyan carries arcs, the meter fill,
  active states and focus rings. Red appears only past −12 dB of reduction, as a
  genuine warning — never decoratively.
- **The Amount knob is the only glowing element.** If everything glows, nothing
  reads as primary.
- **The GR meter is the plugin's pulse.** It must react instantly and visibly;
  a sluggish or timid meter undermines the whole one-knob premise.
- **Type is geometric and sparse.** Uppercase labels at 10 px / 0.12em tracking;
  values at 13 px tabular. No decorative fonts.
- **Motion is short and purposeful.** 120 ms ease on hover and state changes,
  60 fps meter animation. No bouncing, no easing longer than 200 ms.
- Character's zone sub-label is the one piece of explanatory text in the UI —
  it teaches the morph without a manual.

---

## Accessibility & robustness

- All text meets 4.5:1 contrast against its background
- Knobs are focusable (`tabindex`) and respond to arrow keys
- Layout is fixed-size, so no reflow concerns; no media queries needed
- `prefers-reduced-motion` disables the glow pulse and shortens transitions

---

## WebView constraints honored

Per `.agents/troubleshooting/known-issues.yaml` → **webview-008**:

- **ALL JavaScript is inline in a single `<script>` block.** No `type="module"`,
  no `import`/`export`, no `<script src>`. ES6 modules fail silently in JUCE
  WebView.
- All CSS is inline in one `<style>` block. No external stylesheets.
- No external fonts or images — system font stack only, all graphics drawn with
  SVG and Canvas.
- Boot on `document.readyState` rather than `DOMContentLoaded` alone, so the UI
  initializes even if the event already fired.
- `window.__pluginBooted` is set true at the end of init, so C++ can detect a
  dead UI.
