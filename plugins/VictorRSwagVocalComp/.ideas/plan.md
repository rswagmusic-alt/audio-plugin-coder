# Implementation Plan — Victor R Swag Vocal Compressor

**Project:** `VictorRSwagVocalComp`
**Phase:** Plan (strategy)
**Architecture:** see `.ideas/architecture.md`

---

## Complexity Score: 3/5

Score ≥3 → **phased implementation**. Do not attempt this in a single pass; the
macro calibration in particular needs the core chain working and measurable
before it can be tuned.

---

## Implementation Strategy: Phased

### Phase 2.1.1 — Core Processing

Goal: audio passes through both stages and responds to the parameters. Not yet
voiced, not yet calibrated.

- [ ] APVTS layout with the six parameters from `parameter-spec.md`, IDs exactly
      as specified (`amount`, `character`, `mix`, `output`, `sc_hpf`, `bypass`)
- [ ] Cache atomic parameter pointers at construction; no string lookups in audio
- [ ] `prepareToPlay`: size the dry delay line and RMS window from `sampleRate`,
      reset all state
- [ ] Sidechain HPF (TPT 2nd-order) on the detector copy only
- [ ] Stage 1 opto: RMS detector, soft knee, log-domain gain computer, dual-stage
      program-dependent release
- [ ] Stage 2 FET: peak detector, fast attack, hard-ish knee, gain computer
- [ ] Macro mapping engine with the starting curves from `architecture.md`,
      evaluated at control rate
- [ ] Delay-matched dry path and equal-gain mix blend
- [ ] Output trim and ramped bypass
- [ ] `ScopedNoDenormals`, `reset()` correctness

**Exit criteria:** sine and vocal material pass cleanly; every parameter has an
audible, correct-direction effect; no clicks; no NaN/denormal stalls.

### Phase 2.1.2 — Voicing & Calibration

Goal: make it sound like the brief. This is the phase that determines whether the
plugin is good, and it is measurement-driven.

- [ ] Measure gain reduction across the full `amount` sweep on real vocal
      material; fit the auto-makeup curve so perceived loudness stays constant
      (this is the brief's #1 feel requirement — verify by LUFS, not by ear alone)
- [ ] Tune opto release program dependence until sustained passages feel even
      without audible pumping
- [ ] Tune the `character` morph so 0% and 100% are clearly two different
      compressors, with no level jump or click anywhere in between
- [ ] Add and voice the FET asymmetric saturation, scaled by gain reduction
- [ ] Check for aliasing on hot transients at 44.1 kHz; decide on oversampling
      (see risk table — this decision has latency consequences)
- [ ] Author the six factory preset directions from `parameter-spec.md`

**Exit criteria:** the six presets are all usable on a real vocal; `amount`
sweep holds loudness within ~1 LU; no audible aliasing on sibilants.

### Phase 2.1.3 — Polish & Integration

- [ ] Parameter smoothing verified under fast host automation (no zipper, no
      coefficient thrash)
- [ ] Metering bridge: atomic GR publication, UI-side ballistics
- [ ] Edge cases: silence, DC, extreme input levels, sample-rate changes
      (44.1 / 48 / 96 / 192 kHz), mono and stereo, very small and very large
      block sizes
- [ ] State save/restore round-trip through the APVTS
- [ ] If oversampling was added: `setLatencySamples()` reported **and** dry delay
      compensated — verify null test with `mix` at 0%
- [ ] pluginval at strictness 10
- [ ] Real DAW session check

**Exit criteria:** clean pluginval pass, stable in a DAW, no state loss.

---

## Dependencies

**Required JUCE modules:**
- `juce_audio_basics`
- `juce_audio_processors`
- `juce_audio_utils`
- `juce_core`
- `juce_dsp` — filters, envelope helpers, oversampling if needed
- `juce_gui_basics`
- `juce_gui_extra` — `juce::WebBrowserComponent`

**Framework:** JUCE 9 (`_tools/JUCE`, pinned 9.0.1), CMake ≥ 3.22

**UI:** WebView — WKWebView on macOS, no runtime install required. Frontend
assets must be inline or strictly relative per the repo's WebView rules.

**External dependencies:** none. No third-party DSP libraries.

**Validation:** pluginval (`_tools/pluginval`)

---

## Risk Assessment

### High Risk

| Risk | Impact | Mitigation |
|---|---|---|
| **Auto-makeup calibration** — if loudness rises with `amount`, the core promise of the plugin fails | Plugin feels like a volume knob | Measure with LUFS metering across the sweep on real vocals; fit the curve empirically. Budget real time for this; do not guess a formula. |
| **FET saturation aliasing** on fast transients at 44.1 kHz | Harsh, digital artifacts on exactly the sibilants the plugin is meant to control | Test early with hot sibilant material. If oversampling is needed, add it in 2.1.2 — *before* the latency contract is settled — and propagate to `setLatencySamples()` and the dry delay. |
| **Two-stage interaction** — stages fighting each other, producing pumping or over-compression at high `amount` | Unusable top half of the primary knob | Weighted threshold curves (opto leads, FET follows) are already in the architecture; verify GR distribution between stages during 2.1.2 and adjust the exponents. |

### Medium Risk

| Risk | Impact | Mitigation |
|---|---|---|
| **`character` morph discontinuities** | Clicks or level jumps mid-sweep | Crossfade every coefficient; never branch on a threshold. Automate the parameter full-range and listen. |
| **Dry/wet comb filtering** if delay match drifts from actual latency | Hollow, phasey sound at partial `mix` | Null test: `mix` at 0% must be bit-comparable to bypass. Re-run whenever latency changes. |
| **Parameter smoothing under automation** — `amount` drives many coefficients at once | Zipper noise or CPU spikes | Smooth the macro, recompute coefficients at control rate, not per sample. |
| **Program-dependent release state** across sample-rate and block-size changes | Inconsistent behavior between hosts | Derive all time constants from `sampleRate`; exercise `prepareToPlay`/`reset` in the 2.1.3 edge-case matrix. |

### Low Risk

- Sidechain HPF — standard TPT filter, one instance
- Output trim and equal-gain mix blend — trivial
- Ramped bypass — standard pattern
- APVTS setup and state persistence — routine JUCE
- WebView parameter binding — well-trodden in this repo's templates

---

## Sequencing Note

Phase 2.1.2 depends on being able to *hear and measure* the chain, and the UI
metering makes that much easier. The design phase (GR meter, knob layout) should
therefore land before or alongside 2.1.2, not after it.
