# Parameter Spec — Victor R Swag Vocal Compressor

**Project name:** `VictorRSwagVocalComp`
**Phase:** Ideation (dream)
**Status:** Definitive control list for the plan and impl phases

---

## Automatable parameters

These are the parameters exposed to the host. IDs are the APVTS parameter IDs
and must not change after the first release (they are what saved presets and DAW
sessions reference).

| ID | Name | Type | Range | Default | Unit |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `amount` | Amount | Float | 0.0 – 100.0 | 35.0 | % |
| `character` | Character | Float | 0.0 – 100.0 | 50.0 | % |
| `mix` | Mix | Float | 0.0 – 100.0 | 100.0 | % |
| `output` | Output | Float | -12.0 – +12.0 | 0.0 | dB |
| `sc_hpf` | Sidechain HPF | Float | 20.0 – 300.0 | 85.0 | Hz |
| `bypass` | Bypass | Bool | off / on | off | — |

### Parameter detail

**`amount` — Amount** *(primary control)*
The macro that drives both compressor stages. Not a raw threshold.

- Linear taper on the knob; the *mapping* underneath is curved
- Internally lowers both stage thresholds and raises both ratios together,
  weighted so the opto stage engages before the FET stage
- Applies automatic makeup gain so perceived loudness stays roughly constant
  across the sweep
- `0%` = both stages effectively inactive (unity, no gain reduction)
- `35%` (default) = light, always-usable leveling
- `100%` = heavy, obvious, deliberately-squashed
- Smoothing required — this parameter moves several internal coefficients at
  once and must not zipper when automated

**`character` — Character**
Continuous morph of stage balance and time constants.

- `0%` = Smooth / opto-dominant: slow attack, long release, low ratio
- `50%` = Balanced: both stages contribute (default)
- `100%` = Aggressive / FET-dominant: fast attack, snappy release, high ratio,
  slight asymmetric saturation
- Must be a continuous crossfade, not a switch — no clicks or level jumps
  anywhere in the sweep
- Smoothing required

**`mix` — Mix**
Dry/wet blend for parallel compression.

- `100%` (default) = fully compressed
- Equal-gain blend; dry path must be delay-matched to the wet path so the blend
  does not produce comb filtering
- Dry signal is taken pre-compression, post-input

**`output` — Output**
Final trim after the compressor chain and the mix blend.

- Applied post-mix so it scales the final result
- Independent of the Amount auto-makeup gain

**`sc_hpf` — Sidechain HPF**
High-pass filter on the detector path only — does not filter the audio.

- Prevents low-frequency rumble, proximity boom and plosives from triggering
  gain reduction across the whole signal
- Logarithmic taper (frequency control)
- Feeds both stages' detectors
- `85 Hz` default suits most vocals; low end of the range approaches "off"

**`bypass` — Bypass**
True bypass of the whole processing chain.

- Must be click-free (ramped, not switched)
- Should report as the host bypass parameter where the format supports it

---

## Non-automatable UI readouts

Metering, not parameters. Listed here so the design and impl phases account for
them.

| Element | Description | Range |
| :--- | :--- | :--- |
| Gain reduction meter | Live gain reduction, primary feedback surface | 0 to -20 dB |
| Input level | Optional input indicator | -60 to 0 dBFS |
| Output level | Optional output indicator | -60 to 0 dBFS |

The gain-reduction meter is the plugin's main visual feedback and needs a fast
attack / slower release ballistic so it reads as responsive without flickering.
Whether it displays summed reduction or the two stages separately is an open
design-phase decision.

---

## Factory preset directions

Preset content is authored in a later phase; these are the intended starting
points.

| Preset | Amount | Character | Mix | Intent |
| :--- | :--- | :--- | :--- | :--- |
| Init | 35 | 50 | 100 | Default state |
| Intimate Vocal | 25 | 15 | 100 | Soft sung vocals, gentle leveling |
| Pop Lead | 45 | 55 | 100 | Present and controlled |
| Rap Upfront | 60 | 85 | 100 | Dense, forward, aggressive |
| Parallel Crush | 85 | 90 | 45 | Heavy layer blended under the dry vocal |
| Broadcast Even | 40 | 30 | 100 | Spoken word, very consistent level |

---

## Implementation notes carried into the plan phase

1. **Parameter IDs are frozen after first release.** Names and ranges can be
   revised during plan/design; IDs should not change afterward.
2. All continuous parameters need smoothing. `amount` and `character` are the
   critical ones — both drive multiple internal coefficients.
3. The auto-makeup curve inside `amount` must be tuned by measurement, not
   guessed, or the constant-loudness goal fails.
4. Dry/wet delay matching is mandatory for `mix`.
5. `sc_hpf` affects the detector path only — a common implementation mistake is
   filtering the audio path instead.
6. Target zero or near-zero latency; if the plan phase introduces oversampling
   on the FET stage, the resulting latency must be reported to the host and
   compensated in the dry path.
