# Style Guide v1 — Victor R Swag Vocal Compressor

**Theme:** Modern dark neon
**Accent:** Electric cyan
**Framework:** WebView

---

## Color Palette

| Token | Hex | Usage |
|---|---|---|
| `--bg` | `#0B0D10` | Window background |
| `--panel` | `#12161B` | Meter well, dropdown, knob body fill |
| `--line` | `#232A32` | Hairlines, knob tracks, borders |
| `--text` | `#E6EDF3` | Values, primary text |
| `--muted` | `#7D8894` | Labels, wordmark, tick marks |
| `--accent` | `#00F5FF` | Arcs, meter fill, active states, focus |
| `--accent-dim` | `#0A7F88` | Inactive arc tint, hover on muted elements |
| `--warn` | `#FF4D6D` | GR meter past −12 dB only |

**Glow recipe (Amount knob only):**
```css
filter: drop-shadow(0 0 6px rgba(0, 245, 255, calc(0.25 + var(--v) * 0.45)));
```
`--v` is the normalized value 0–1, so the glow intensifies as the knob turns.

**Rule:** cyan is the only chromatic color in the interface. Red appears solely
as a gain-reduction warning past −12 dB. Do not introduce a third hue.

---

## Typography

System stack, no web fonts (WebView has no network access):

```css
font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display",
             "Segoe UI", Inter, system-ui, sans-serif;
```

| Role | Size | Weight | Tracking | Transform | Color |
|---|---|---|---|---|---|
| Wordmark | 12 px | 600 | 0.18em | uppercase | `--muted` |
| Control label | 10 px | 600 | 0.12em | uppercase | `--muted` |
| Value readout | 13 px | 500 | 0.02em | — | `--text` |
| Zone sub-label | 9 px | 500 | 0.10em | uppercase | `--accent-dim` |
| Meter readout | 12 px | 500 | 0.02em | — | `--text` |
| Meter ticks | 8 px | 400 | 0.06em | — | `--muted` |

All numeric readouts use `font-variant-numeric: tabular-nums` so values do not
shift horizontally while dragging.

---

## Spacing

8 px base unit. Every gap is a multiple of it.

| Token | Value | Usage |
|---|---|---|
| `--pad` | 24 px | Window edge padding |
| `--gap-sm` | 8 px | Label → control, value → control |
| `--gap-md` | 16 px | Header → hero |
| `--gap-lg` | 32 px | Between hero knobs |

---

## Control Visual Styles

### Rotary knobs
- **Geometry:** 270° sweep, −135° to +135°, drawn as an SVG circle with
  `stroke-dasharray`, rotated 135° so the gap sits at the bottom
- **Track:** `--line`, `stroke-linecap: round`
- **Arc:** `--accent`, `stroke-linecap: round`
- **Stroke widths:** hero 7 px · secondary 5 px · mini 4 px (viewBox units)
- **Body:** `--panel` fill circle inset 6 px from the arc
- **Indicator:** 2 px `--text` line from 55% to 88% of radius, rotated to value
- **Bipolar mode** (`output` only): arc originates at 12 o'clock and grows in
  either direction; a 1 px `--muted` notch marks center

| Size | Diameter | Stroke | Used by |
|---|---|---|---|
| Hero | 150 px | 7 | `amount` |
| Secondary | 90 px | 5 | `character`, `mix` |
| Mini | 56 px | 4 | `sc_hpf`, `output` |

**States:** rest = arc at 85% opacity · hover = 100% + track lightens to `#2C343D`
· dragging = indicator brightens, `cursor: ns-resize` · focus = 1 px `--accent`
ring at 40% opacity.

### Pill toggle (`bypass`)
- 96 × 32 px, 16 px radius, 1 px `--line` border, `--muted` label
- **Active:** `--accent` background, `#06181B` label, entire `#plugin-ui` drops
  to 45% opacity with a 120 ms transition

### Preset dropdown
- Native `<select>`, restyled: `--panel` background, 1 px `--line` border,
  6 px radius, 12 px text, custom cyan chevron drawn as an inline SVG
  background-image (no external asset)

### Gain-reduction meter
- Canvas, 592 × 44 CSS px, backed at `devicePixelRatio` for crisp rendering
- **Well:** `--panel`, 4 px radius
- **Summed fill:** left→right linear gradient `--accent-dim` → `--accent`;
  shifts to `--warn` past −12 dB
- **Hover split:** two 14 px bars, 4 px gap — `OPTO` above, `FET` below, each
  with a 24 px `--muted` caption on the left
- **Ticks:** 1 px `--line` verticals at −3, −6, −12, −20 dB with 8 px captions
- **Ballistics:** visual peak follows instantly on attack, decays ~180 ms

---

## Motion

| Interaction | Duration | Easing |
|---|---|---|
| Hover states | 120 ms | `ease-out` |
| Bypass dim | 120 ms | `ease-out` |
| Meter hover split crossfade | 180 ms | `ease-in-out` |
| Meter value animation | per-frame (60 fps) | — |
| Knob value change | none (immediate) | — |

Knobs never animate toward a value — a lagging knob feels broken under the hand.
Only the meter and state transitions animate.

Under `prefers-reduced-motion: reduce`, the Amount glow stops pulsing and all
transitions drop to 0 ms.

---

## Example UI States

**Idle / Init** — Amount at 35%, arc just past the first third with a soft glow.
Meter well empty, readout `-0.0 dB`. Character reads `BALANCED`.

**Working / Pop Lead** — Amount at 45%, glow clearly lit. Meter bar breathing
between −2 and −6 dB in cyan, readout tracking. The whole UI reads "alive."

**Heavy / Parallel Crush** — Amount at 85%, glow at maximum. Meter deep into the
bar and tinted `--warn`, sitting around −14 dB. Mix pulled back to 45%, so its
arc is visibly short — the contrast between a full Amount arc and a short Mix arc
communicates parallel compression at a glance.

**Bypassed** — Whole interface at 45% opacity, BYPASS pill filled cyan, meter
frozen and empty.

**Meter hovered** — Summed bar crossfades into OPTO and FET rows. At low Amount
the OPTO bar dominates; at high Amount with Character near 100 the FET bar takes
over. This is the plugin explaining its own topology.

---

## Do / Don't

**Do**
- Keep cyan as the single accent
- Let the Amount knob be visually dominant
- Use tabular figures everywhere a number changes
- Draw everything with SVG and Canvas

**Don't**
- Add gradients to backgrounds or panels (flat only)
- Add a second glowing element
- Use drop shadows to fake physical depth
- Reference external fonts, images, or scripts — they fail silently in WebView
