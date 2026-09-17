# TODO — Victor R Swag Vocal Compressor

Task tracking across APC phases. Checked items are complete.

## Phase 1 — DREAM (ideation) ✅

- [x] Creative brief (`.ideas/creative-brief.md`)
- [x] Parameter spec (`.ideas/parameter-spec.md`)
- [x] Initialize `status.json`

## Phase 2 — PLAN (architecture) ✅

- [x] DSP architecture (`.ideas/architecture.md`)
- [x] Implementation plan (`.ideas/plan.md`)
- [x] Complexity assessment — 3/5, phased strategy
- [x] UI framework selection — WebView

## Phase 3 — DESIGN (GUI) ✅

- [x] Requirements gathering (window size, accent, meter mode, header)
- [x] UI specification (`Design/v1-ui-spec.md`)
- [x] Style guide (`Design/v1-style-guide.md`)
- [x] WebView preview (`Design/v1-test.html`)
- [x] Knob interaction model — drag / shift-fine / dbl-click reset / wheel / keys
- [x] Gain-reduction meter — summed bar with per-stage split on hover
- [x] Six factory presets wired in the preview
- [x] webview-008 compliance verified (single inline script, no modules)
- [x] Element IDs cross-checked against `parameter-spec.md`
- [x] Design approved → `design_complete`

## Phase 4 — IMPL (DSP + UI integration)

### 2.1.1 Core processing
- [ ] APVTS layout with the six parameter IDs
- [ ] Cache atomic parameter pointers (no string lookups in audio thread)
- [ ] `prepareToPlay` — size dry delay line and RMS window from `sampleRate`
- [ ] Sidechain HPF (TPT 2nd-order) on the detector copy only
- [ ] Stage 1 opto — RMS detector, soft knee, dual-stage program-dependent release
- [ ] Stage 2 FET — peak detector, fast attack, hard-ish knee
- [ ] Macro mapping engine at control rate
- [ ] Delay-matched dry path + equal-gain mix blend
- [ ] Output trim + ramped bypass
- [ ] `ScopedNoDenormals`, correct `reset()`

### 2.1.2 Voicing & calibration ✅
- [x] Fit auto-makeup curve by LUFS measurement across the Amount sweep
      (measured 11×5 table + bilinear interp; worst residual +0.141 LU)
- [x] Fix amount=0 unity — thresholds now start at +10 dBFS
- [x] Tune opto release program dependence
      (7.3 dB GR → 334 ms recovery; 10.7 dB GR → 1008 ms)
- [x] Tune Character morph — worst step 0.15 LU, no clicks or jumps
- [x] FET asymmetric saturation scaled by gain reduction (h2 at −35 dB)
- [x] Aliasing check at 44.1 kHz — all fold-back < −77 dB, NO oversampling
      needed, latency stays at zero
- [x] Six factory presets authored (UI dropdown, values match parameter-spec)
- [x] `tools/calibrate.cpp` measurement harness added

### 2.1.3 Polish & integration ✅
- [x] Port `Design/v1-test.html` → `Source/ui/public/index.html` (preview
      simulation dropped, GR meter bound to C++ "grUpdate" events)
- [x] Parameter smoothing verified under fast host automation (randomised
      macros every 64 samples → output stays finite)
- [x] Metering bridge — atomic GR publication, 30 Hz timer, UI-side ballistics
- [x] Edge cases — silence, DC, 0 dBFS, 1e-30 denormals, impulse, across
      44.1/48/96/192 kHz. All finite, silence in → silence out.
- [x] **Fixed: DC / sub-sonic content was boosted up to +15.7 dBFS.** The
      sidechain HPF hides it from the detector, so no compression happened but
      full makeup was still applied. Makeup is now scaled by a 400 ms average
      of the gain reduction actually achieved (see `kExpectedGrTable`).
- [x] Makeup table re-converged after the fix — worst residual −0.377 LU
- [x] Oversized-block safety — processor and engine both chunk rather than
      overrun their scratch buffers
- [x] `reset()` override; state restore snaps smoothers via an atomic flag
      rather than touching them from the message thread
- [x] No oversampling needed → latency 0 → dry delay stays at 0 samples;
      `mix`=0 reduces to `dry * outGain` exactly

### Deferred to TEST phase
- [ ] pluginval strictness 10 — **blocked**: `_tools/pluginval_bin` is absent
      on macOS and `scripts/test-plugin.sh` (referenced by `hub/server.js`)
      does not exist in this checkout
- [ ] `auval` — needs the AU installed into ~/Library/Audio/Plug-Ins/Components
- [ ] Real-vocal re-validation of the makeup table

## Phase 5 — TEST

- [ ] pluginval at strictness 10
- [ ] Real DAW session check
- [ ] Crash/stability analysis

## Phase 6 — SHIP

- [ ] Installer / release archive
- [ ] User documentation
- [ ] Version bump to v1.0.0
