# Creative Brief — Victor R Swag Vocal Compressor

**Project name (directory / CMake target):** `VictorRSwagVocalComp`
**Product / display name:** Victor R Swag Vocal Compressor
**Short name (DAW plugin list):** VRS Vocal Comp
**Phase:** Ideation (dream)
**Format targets:** VST3 + AU (macOS), Standalone for preview

---

## Hook

**Two legendary compressors. One knob. Your vocal, finished.**

Victor R Swag Vocal Compressor chains a slow optical leveler into a fast FET
peak-catcher — the signal path pro engineers have been wiring by hand for
decades — and puts the whole thing behind a single **Amount** knob. Turn it up
until the vocal sits. That's the workflow.

---

## Description

### The concept

Most vocal compressors force a choice: smooth and forgiving, or fast and
aggressive. Real vocal chains never make that choice — they use both, in series.
A gentle optical stage rides the long-term level so the performance stays even,
then a fast FET stage catches the transient spikes that the opto is too slow to
see. The result is a vocal that is both consistent *and* punchy, without either
stage having to work hard enough to sound like it's working.

This plugin is that chain, pre-wired and voiced for vocals, with the setup
decisions already made.

### Signal flow

```
Input → Sidechain HPF → [Stage 1: OPTO leveler] → [Stage 2: FET peak comp] → Output trim
                                    ↓                        ↓
                              (gain reduction metering, summed)
                                    │
Input ──────────────────────────────┴──────────────→ Mix (parallel blend) → Out
```

- **Stage 1 — Opto leveler.** Program-dependent, soft-knee, dual-stage release
  (fast initial recovery, slow tail). Low ratio, catches the slow swells. This is
  what makes a take feel "even" rather than "compressed."
- **Stage 2 — FET peak comp.** Fast attack, higher ratio, hard-ish knee. Catches
  consonants, plosive edges and shouted syllables that slip past the opto. Adds
  the forward, dense quality that makes a vocal sit on top of a mix.
- **Sidechain HPF.** Keeps low-end rumble, proximity boom and plosives from
  pumping the whole chain. Vocal-specific and genuinely necessary.
- **Parallel mix.** Full wet is the default, but the blend lets the user keep
  natural dynamics under a heavily-compressed layer.

### The Amount macro

`Amount` is the primary control and the whole point of the plugin. It is not a
threshold — it is a coordinated move across both stages:

- Lowers both thresholds together, weighted so the opto engages first
- Raises both ratios on a curve (gentle at low settings, firm at high)
- Applies automatic makeup gain so perceived loudness stays roughly constant
  as the knob turns — the user hears *compression*, not *volume*

The goal: turning Amount up should never make the vocal sound louder, only more
present and more controlled. That auto-makeup behavior is the single most
important feel requirement in this plugin.

### The Character morph

`Character` rebalances which stage does the work and how it behaves, on a
continuous morph:

- **0% — Smooth.** Opto-dominant. Slow attack, long release, low ratio.
  Forgiving. For delicate, intimate, sung vocals.
- **50% — Balanced.** Both stages contribute. The default. General-purpose.
- **100% — Aggressive.** FET-dominant. Very fast attack, snappy release, high
  ratio, with a touch of asymmetric saturation. For rap, pop and anything that
  needs to punch through a dense mix.

One knob, two ends, a usable continuum between them. No mode switch.

### Voicing intent

- Subtle harmonic coloration that increases with gain reduction — the plugin
  should sound slightly *richer* when it's working hard, never thinner
- No aliasing artifacts on hot transients; the FET stage's fast attack is the
  risk area
- Latency: zero or near-zero. This is a tracking and mixing tool.

### Target user

Vocalists and producers who record and mix their own vocals, know what a
compressed vocal should sound like, and do not want to spend ten minutes dialing
attack and release. Also: engineers who want a fast first-pass vocal chain
before reaching for surgical tools.

### What this plugin is not

- Not a multiband or dynamic EQ
- Not a de-esser (the sidechain HPF is filtering, not de-essing)
- Not a mastering compressor — it is voiced specifically for vocals
- Not an emulation of any specific named hardware unit; it takes the *topology*
  of the classic chain, not a model of a particular box

---

## Visual direction

**Modern dark neon.**

- Flat dark charcoal panel, no skeuomorphic metal or screws
- One saturated accent color that carries the whole interface (working
  direction: electric cyan or acid green — final call in the design phase)
- **Amount** is the visual hero: large central knob with a glowing arc that
  fills as it turns
- **Character** and **Mix** are secondary, smaller, flanking
- Live gain-reduction readout as a horizontal bar that lights along the accent
  color and reacts fast — this is the plugin's main feedback surface and it
  needs to feel alive
- Clean geometric sans typography, generous spacing, no visual clutter
- Subtle glow and smooth animated transitions; nothing that costs frames

The UI should read as contemporary and confident, matching the artist-brand
name without resorting to gold-chrome pastiche.

---

## Success criteria

1. A user can get a usable, mix-ready vocal with **Amount** alone in under ten
   seconds
2. Perceived loudness stays stable across the full Amount sweep
3. `Character` at 0% and 100% sound clearly like two different compressors
4. The gain-reduction meter makes it obvious *why* the vocal is changing
5. Clean pluginval pass and stable performance in a real DAW session

---

## Open questions for the plan phase

- Opto and FET stage implementation approach, and whether any oversampling is
  needed on the FET stage to keep fast-attack transients clean
- Exact auto-makeup curve — measured against the Amount sweep
- Whether the gain-reduction meter shows summed reduction or the two stages
  separately (separate is more informative, but busier)
- UI framework confirmation (repo default is WebView, which suits the animated
  neon meter well)
