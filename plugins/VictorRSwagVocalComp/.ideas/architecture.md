# DSP Architecture Specification — Victor R Swag Vocal Compressor

**Project:** `VictorRSwagVocalComp`
**Phase:** Plan (architecture)
**Source contracts:** `.ideas/creative-brief.md`, `.ideas/parameter-spec.md`

---

## Core Components

| # | Component | Responsibility |
|---|---|---|
| 1 | Bypass ramp | Click-free engage/disengage of the whole chain |
| 2 | Dry tap + delay match | Captures the dry signal and delays it to match wet-path latency |
| 3 | Sidechain HPF | 2nd-order high-pass on the **detector path only**, shared by both stages |
| 4 | Stage 1 — Opto leveler | Slow, program-dependent RMS leveling, soft knee, low ratio |
| 5 | Stage 2 — FET peak comp | Fast peak catching, hard-ish knee, high ratio, asymmetric saturation |
| 6 | Macro mapping engine | Translates `amount` + `character` into per-stage coefficients |
| 7 | Auto-makeup gain | Loudness compensation derived from the `amount` mapping |
| 8 | Mix blend | Equal-gain parallel blend of delay-matched dry and wet |
| 9 | Output trim | Final post-mix gain |
| 10 | Metering bridge | Lock-free gain-reduction / level reporting to the UI |

### Component detail

**3. Sidechain HPF**
State-variable or TPT 2nd-order high-pass, cutoff = `sc_hpf`. Processes a copy of
the input used *only* for detection. The audio path is never filtered. One
filter instance feeds both stage detectors — they observe the same cleaned
control signal, which keeps the two stages coherent.

**4. Stage 1 — Opto leveler**
- Detector: RMS-style envelope (windowed mean-square, ~10 ms window)
- Attack: 10–30 ms, Release: dual-stage — fast initial recovery (~80 ms) into a
  slow tail (~600 ms–2 s), with the crossover weighted by how much reduction is
  currently applied. This program dependence is what makes opto behavior feel
  musical and is the defining characteristic of the stage.
- Knee: soft, ~12 dB
- Ratio: low, 1.5:1 → 4:1 depending on the macros
- Gain computer operates in the log domain

**5. Stage 2 — FET peak comp**
- Detector: peak envelope with fast attack
- Attack: 0.1–8 ms, Release: 40–250 ms
- Knee: ~3 dB (hard-ish)
- Ratio: 4:1 → 20:1 depending on the macros
- Asymmetric soft-clip saturation applied post-gain-reduction, scaled by the
  amount of reduction — the "richer when working harder" voicing requirement
  from the brief
- This is the aliasing risk area; see the risk table in `plan.md`

**6. Macro mapping engine**
Not a DSP block in the signal path — a coefficient calculator run at control
rate (per block) that owns the relationship between the two macro knobs and the
eight-or-so internal values the stages actually need. Centralizing it here keeps
the stages dumb and the mapping tunable in one place.

**7. Auto-makeup gain**
A function of `amount` (and secondarily `character`, since FET-dominant settings
reduce more), applied inside the wet path before the mix. Must be derived from
measurement of the actual chain, not from a theoretical `(1 - 1/ratio)` formula —
the two-stage topology makes the closed form wrong.

**8. Mix blend**
Equal-gain (not equal-power) blend, since dry and wet are correlated. The dry
path is delayed to match total wet-path latency so the blend cannot comb-filter.

---

## Processing Chain

```
                 ┌──────────────────────────────────────────────┐
                 │            DETECTOR PATH (control)           │
 Input ─────┬───▶│  Sidechain HPF ──┬──▶ Opto detector          │
            │    │                  └──▶ FET detector           │
            │    └───────────┬──────────────────┬───────────────┘
            │                │ GR1              │ GR2
            │                ▼                  ▼
            ├──────▶ [Stage 1: OPTO] ──▶ [Stage 2: FET + sat] ──▶ Auto-makeup ──┐
            │                                                                    │
            │                                                                    ▼
            └──────▶ Dry delay match ──────────────────────────────────────▶ [ Mix ] ──▶ Output trim ──▶ Bypass ramp ──▶ Out
                                                                                 ▲
                                                                         GR1+GR2 ──▶ Metering bridge ──▶ UI
```

Order rationale: opto before FET so the fast stage sees an already-leveled
signal and only has to catch true transients. Reversing the order would make the
FET stage do most of the work and defeat the topology.

---

## Parameter Mapping

| Parameter | Component(s) | Function | Internal range |
|---|---|---|---|
| `amount` (0–100%) | Macro engine → both stages | Lowers both thresholds, raises both ratios, drives auto-makeup | Opto threshold 0 → −30 dB; FET threshold −6 → −34 dB; ratios scale on curve |
| `character` (0–100%) | Macro engine → both stages | Morphs stage balance, time constants and saturation | Opto weight 1.0 → 0.35; FET weight 0.2 → 1.0; FET attack 8 → 0.1 ms; sat drive 0 → 1 |
| `mix` (0–100%) | Mix blend | Equal-gain dry/wet crossfade | 0.0 – 1.0 linear |
| `output` (−12…+12 dB) | Output trim | Post-mix gain | 0.251 – 3.98 linear |
| `sc_hpf` (20–300 Hz) | Sidechain HPF | Detector-path cutoff | Log-mapped, Q = 0.707 |
| `bypass` (bool) | Bypass ramp | Ramped full-chain bypass | 5–10 ms ramp |

### Macro mapping curves (starting points, to be tuned in impl)

**`amount` → thresholds**
- Opto threshold: `0 dB − (amount/100)^0.8 × 30 dB` — engages early
- FET threshold: `−6 dB − (amount/100)^1.2 × 28 dB` — engages later
- The differing exponents are what makes the opto lead and the FET follow.

**`amount` → ratios**
- Opto ratio: `1.5 + (amount/100)^1.5 × 2.5` → 1.5:1 … 4:1
- FET ratio: `4 + (amount/100)^1.3 × 16` → 4:1 … 20:1

**`character` → timing**
- Opto attack: `30 ms → 10 ms`; opto release tail: `2000 ms → 600 ms`
- FET attack: `8 ms → 0.1 ms` (exponential taper); FET release: `250 ms → 40 ms`
- Stage weights crossfade as in the table above, so neither stage ever hard-cuts

**`character` → saturation**
- Drive: `0 → 1`, applied only to the FET stage output and further scaled by
  instantaneous gain reduction

---

## Real-Time Safety Contract

- No allocation, locking, logging, or file I/O in `processBlock`
- All buffers (dry delay line, RMS window) sized in `prepareToPlay` from
  `sampleRate` and `maximumExpectedSamplesPerBlock`
- All parameter reads via cached atomic pointers from the APVTS, never by
  string lookup in the audio thread
- `juce::SmoothedValue` on every continuous parameter; `amount` and `character`
  additionally recompute coefficients at control rate (per block), not per sample
- Denormal protection via `juce::ScopedNoDenormals` in `processBlock`
- Metering published through `std::atomic<float>`, polled by the UI timer —
  never pushed from the audio thread
- `reset()` clears all envelope and filter state; must be safe to call at any time

---

## Latency

Target: **zero reported latency.** No lookahead is used — the FET stage's fast
attack replaces it, which is faithful to the hardware topology anyway.

If oversampling is added to the FET stage during impl, `juce::dsp::Oversampling`
introduces filter latency that **must** be (a) reported via
`setLatencySamples()` and (b) added to the dry delay-match line, or the parallel
mix will comb-filter. This is the single most likely source of a subtle,
hard-to-diagnose bug in this plugin.

---

## Complexity Assessment

**Score: 3/5 (Advanced)**

**Rationale:**

Level 3 is the honest rating. The individual building blocks are well-understood
(envelope followers, log-domain gain computers, TPT filters, a delay line) — none
of this is research DSP, so it is not Level 4. But it is clearly past Level 2:

- **Two interacting dynamics stages in series.** Their combined behavior is not
  the sum of their parts; tuning one changes how the other is fed.
- **Macro mapping is the hard part.** Two knobs drive roughly eight internal
  coefficients through curves that must be *tuned by ear and by measurement*.
  Getting `amount` to hold perceived loudness constant is a calibration task, not
  a coding task, and is the main source of schedule risk.
- **Program-dependent dual-stage release** in the opto detector is real state
  management, not a one-pole filter.
- **Delay-matched parallel mix** must stay correct if latency ever changes.
- **Continuous morph with no clicks** across the full `character` sweep requires
  care at every coefficient crossfade.
- **Saturation** brings an aliasing risk that may force oversampling, which then
  feeds back into the latency contract.

What keeps it off Level 4: no synthesis engine, no complex feedback topology, no
real-time spectral analysis, no ML, no multiband splitting.
