/*
  ==============================================================================

    calibrate.cpp - VictorRSwagVocalComp offline measurement harness

    Phase 2.1.2 of .ideas/plan.md requires the auto-makeup curve to be FITTED
    FROM MEASUREMENT, not from a closed-form (1 - 1/ratio) estimate - two
    stages in series with independent thresholds do not compose that way.

    This harness includes the REAL MacroMap.h and CompressorStages.h, so the
    curves it measures are the curves the plugin ships. The outer chain
    arithmetic (detector HPF, stage weighting, gain application) is mirrored
    from VocalCompEngine::process() - keep the two in sync if either changes.

    Measurements:
      1. loudness   - LKFS (ITU-R BS.1770 K-weighting) delta across the amount
                      sweep, with makeup disabled -> required makeup curve
      2. continuity - largest gain step across a fine character sweep, to catch
                      clicks or level jumps in the morph
      3. aliasing   - Goertzel probes on the fold-back bins of a hot HF sine,
                      to decide whether the FET saturation needs oversampling

    Build:  clang++ -std=c++17 -O2 -o /tmp/vrs_calibrate tools/calibrate.cpp
    Run:    /tmp/vrs_calibrate

  ==============================================================================
*/

#include "../Source/dsp/MacroMap.h"
#include "../Source/dsp/CompressorStages.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include <string>

namespace
{
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Topology-preserving state variable filter, highpass.
// Mirrors juce::dsp::StateVariableTPTFilter<float> in highpass mode.
//==============================================================================
class TptHighpass
{
public:
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() { s1 = s2 = 0.0f; }

    void setCutoff (float hz)
    {
        const float g = std::tan (static_cast<float> (kPi) * hz / static_cast<float> (sampleRate));
        const float k = 1.0f / kResonance;
        h  = 1.0f / (1.0f + g * (g + k));
        g1 = g;
        k1 = k;
    }

    float process (float x)
    {
        const float hp = h * (x - (g1 + k1) * s1 - s2);
        const float v1 = g1 * hp;
        const float bp = v1 + s1;
        s1 = bp + v1;
        const float v2 = g1 * bp;
        const float lp = v2 + s2;
        s2 = lp + v2;
        return hp;
    }

private:
    static constexpr float kResonance = 0.707f;
    double sampleRate = 48000.0;
    float h = 0.0f, g1 = 0.0f, k1 = 0.0f, s1 = 0.0f, s2 = 0.0f;
};

//==============================================================================
// ITU-R BS.1770 K-weighting + mean square -> LKFS.
// Coefficients are the standard 48 kHz set.
//==============================================================================
class Biquad
{
public:
    Biquad (double b0_, double b1_, double b2_, double a1_, double a2_)
        : b0 (b0_), b1 (b1_), b2 (b2_), a1 (a1_), a2 (a2_) {}

    double process (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }

    void reset() { x1 = x2 = y1 = y2 = 0.0; }

private:
    double b0, b1, b2, a1, a2;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

double measureLkfs (const std::vector<float>& signal)
{
    // Stage 1: high-shelf. Stage 2: high-pass (RLB).
    Biquad shelf  { 1.53512485958697, -2.69169618940638, 1.19839281085285,
                   -1.69065929318241,  0.73248077421585 };
    Biquad rlb    { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };

    double sum = 0.0;
    for (float s : signal)
    {
        const double w = rlb.process (shelf.process (static_cast<double> (s)));
        sum += w * w;
    }

    const double meanSquare = sum / std::max<size_t> (1, signal.size());
    return -0.691 + 10.0 * std::log10 (std::max (meanSquare, 1e-12));
}

//==============================================================================
// Goertzel magnitude at one frequency - cheaper and more precise than an FFT
// when you already know which bins matter.
//==============================================================================
double goertzel (const std::vector<float>& x, double freq, double sampleRate)
{
    const size_t n = x.size();
    const double w = 2.0 * kPi * freq / sampleRate;
    const double coeff = 2.0 * std::cos (w);

    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        s0 = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    const double real = s1 - s2 * std::cos (w);
    const double imag = s2 * std::sin (w);
    return 2.0 * std::sqrt (real * real + imag * imag) / static_cast<double> (n);
}

//==============================================================================
// Synthetic vocal-like test signal.
//
// NOT a real vocal recording. It reproduces the features that drive a vocal
// compressor - syllable-rate amplitude modulation, phrase-level swells, a
// harmonic stack with vibrato, sibilant noise bursts and plosive transients -
// which is what the makeup fit actually responds to. The fit MUST be
// re-validated against real material before release.
//==============================================================================
std::vector<float> makeVocalSignal (double sampleRate, double seconds, float targetRmsDb)
{
    const size_t n = static_cast<size_t> (sampleRate * seconds);
    std::vector<float> out (n, 0.0f);

    std::mt19937 rng { 20260917 };
    std::uniform_real_distribution<float> noise { -1.0f, 1.0f };

    const double f0 = 180.0;

    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;

        // vibrato on the fundamental
        const double vib = 1.0 + 0.04 * std::sin (2.0 * kPi * 5.0 * t);

        // harmonic stack, 1/n rolloff
        double voiced = 0.0;
        for (int h = 1; h <= 12; ++h)
            voiced += std::sin (2.0 * kPi * f0 * vib * h * t) / h;
        voiced /= 3.1;

        // syllable envelope: sharp attack, slower decay, ~4 Hz
        const double syl = std::pow (std::max (0.0, std::sin (2.0 * kPi * 4.0 * t)), 0.35);

        // phrase swell
        const double phrase = 0.45 + 0.55 * (0.5 + 0.5 * std::sin (2.0 * kPi * 0.35 * t));

        // sibilance: HF noise riding on every third syllable
        const double sibGate = (std::sin (2.0 * kPi * 1.33 * t) > 0.75) ? 1.0 : 0.0;
        const double sib = sibGate * 0.35 * noise (rng);

        // plosives: rare, fast, loud - the transients the FET stage exists for
        const double plos = std::pow (std::max (0.0, std::sin (2.0 * kPi * 0.6 * t + 0.9)), 40.0) * 1.6;

        out[i] = static_cast<float> ((voiced * syl * phrase + sib * syl + plos * voiced) * 0.5);
    }

    // normalise to a realistic tracked-vocal level
    double sum = 0.0;
    for (float s : out) sum += static_cast<double> (s) * s;
    const double rms = std::sqrt (sum / static_cast<double> (n));
    const double target = std::pow (10.0, targetRmsDb / 20.0);
    const float scale = static_cast<float> (target / std::max (rms, 1e-9));

    for (float& s : out) s *= scale;
    return out;
}

std::vector<float> makeSine (double sampleRate, double seconds, double freq, double peakDb)
{
    const size_t n = static_cast<size_t> (sampleRate * seconds);
    std::vector<float> out (n);
    const double amp = std::pow (10.0, peakDb / 20.0);
    for (size_t i = 0; i < n; ++i)
        out[i] = static_cast<float> (amp * std::sin (2.0 * kPi * freq * i / sampleRate));
    return out;
}

//==============================================================================
// Mirrors VocalCompEngine::process() for a mono signal.
// makeupEnabled=false measures the raw loss the makeup curve has to cancel.
//==============================================================================
struct ChainResult
{
    std::vector<float> output;
    float peakOptoGr = 0.0f;
    float peakFetGr  = 0.0f;
    float meanTotalGr = 0.0f;
};

ChainResult runChain (const std::vector<float>& input, double sampleRate,
                      float amountPercent, float characterPercent,
                      float scHpfHz, bool makeupEnabled)
{
    const float amountN = amountPercent * 0.01f;
    const float charN   = characterPercent * 0.01f;
    const vrs::MacroCoefficients c = vrs::mapMacros (amountN, charN);

    vrs::OptoStage opto;
    vrs::FetStage  fet;
    opto.prepare (sampleRate);
    fet.prepare  (sampleRate);
    opto.setCoefficients (c.optoThresholdDb, c.optoRatio, c.optoKneeDb,
                          c.optoAttackMs, c.optoReleaseFastMs, c.optoReleaseSlowMs);
    fet.setCoefficients  (c.fetThresholdDb, c.fetRatio, c.fetKneeDb,
                          c.fetAttackMs, c.fetReleaseMs);

    TptHighpass hpf;
    hpf.prepare (sampleRate);
    hpf.setCutoff (scHpfHz);

    // Mirrors VocalCompEngine: makeup scaled by a 400 ms average of the gain
    // reduction actually achieved, normalised by the expected GR for this macro
    // setting. Keep in sync with the engine.
    const float slowCoeff = 1.0f - std::exp (-1.0f / (0.4f * static_cast<float> (sampleRate)));
    float slowGrDb = 0.0f;

    ChainResult r;
    r.output.resize (input.size());

    double grSum = 0.0;

    for (size_t i = 0; i < input.size(); ++i)
    {
        const float x = input[i];
        const float detector = hpf.process (x);

        const float optoGrDb = opto.processSample (detector) * c.optoWeight;
        const float optoGain = vrs::dbToGain (-optoGrDb);

        const float fetGrDb = fet.processSample (detector * optoGain) * c.fetWeight;
        const float fetGain = vrs::dbToGain (-fetGrDb);

        const float satDrive = c.satDrive * std::clamp (fetGrDb / 8.0f, 0.0f, 1.0f);
        const float compressed = x * optoGain * fetGain;

        const float activity = (c.expectedGrDb > 0.05f)
                             ? std::clamp (slowGrDb / c.expectedGrDb, 0.0f, 1.0f)
                             : 0.0f;
        const float makeup = makeupEnabled ? vrs::dbToGain (c.makeupDb * activity) : 1.0f;

        r.output[i] = vrs::saturateAsymmetric (compressed, satDrive) * makeup;

        slowGrDb += ((optoGrDb + fetGrDb) - slowGrDb) * slowCoeff;

        r.peakOptoGr = std::max (r.peakOptoGr, optoGrDb);
        r.peakFetGr  = std::max (r.peakFetGr,  fetGrDb);
        grSum += optoGrDb + fetGrDb;
    }

    r.meanTotalGr = static_cast<float> (grSum / static_cast<double> (input.size()));
    return r;
}


//==============================================================================
// MANUAL mode chain. Mirrors VocalCompEngine::applyManualOverride() - both
// stages take the user's threshold/ratio/attack/release verbatim, while
// CHARACTER still sets the opto<->FET weighting and the saturation drive.
//==============================================================================
ChainResult runChainManual (const std::vector<float>& input, double sampleRate,
                            float thresholdDb, float ratioValue,
                            float attackMs, float releaseMs,
                            float characterPercent)
{
    const float charN = characterPercent * 0.01f;
    vrs::MacroCoefficients c = vrs::mapMacros (0.5f, charN);

    c.optoThresholdDb = thresholdDb;  c.fetThresholdDb = thresholdDb;
    c.optoRatio       = ratioValue;   c.fetRatio       = ratioValue;
    c.optoAttackMs    = attackMs;     c.fetAttackMs    = attackMs;
    c.optoReleaseFastMs = releaseMs * 0.35f;
    c.optoReleaseSlowMs = releaseMs;
    c.fetReleaseMs      = releaseMs;

    const float over  = std::max (0.0f, -thresholdDb);
    const float slope = 1.0f - 1.0f / std::max (1.0f, ratioValue);
    c.makeupDb     = std::clamp (0.50f * slope * over, 0.0f, 24.0f);
    c.expectedGrDb = std::clamp (0.35f * slope * over, 0.05f, 24.0f);

    vrs::OptoStage opto; vrs::FetStage fet; TptHighpass hpf;
    opto.prepare (sampleRate); fet.prepare (sampleRate);
    hpf.prepare (sampleRate);  hpf.setCutoff (85.0f);
    opto.setCoefficients (c.optoThresholdDb, c.optoRatio, c.optoKneeDb,
                          c.optoAttackMs, c.optoReleaseFastMs, c.optoReleaseSlowMs);
    fet.setCoefficients  (c.fetThresholdDb, c.fetRatio, c.fetKneeDb,
                          c.fetAttackMs, c.fetReleaseMs);

    ChainResult r; r.output.resize (input.size());
    double grSum = 0.0;

    for (size_t i = 0; i < input.size(); ++i)
    {
        const float d = hpf.process (input[i]);
        const float optoGr = opto.processSample (d) * c.optoWeight;
        const float og = vrs::dbToGain (-optoGr);
        const float fetGr = fet.processSample (d * og) * c.fetWeight;
        const float fg = vrs::dbToGain (-fetGr);
        r.output[i] = (input[i] * og * fg);
        r.peakOptoGr = std::max (r.peakOptoGr, optoGr);
        r.peakFetGr  = std::max (r.peakFetGr,  fetGr);
        grSum += optoGr + fetGr;
    }
    r.meanTotalGr = static_cast<float> (grSum / static_cast<double> (input.size()));
    return r;
}

} // namespace

//==============================================================================
int main()
{
    const double sr = 48000.0;
    const auto vocal = makeVocalSignal (sr, 12.0, -18.0f);
    const double inLkfs = measureLkfs (vocal);

    std::printf ("VictorRSwagVocalComp - calibration harness\n");
    std::printf ("Signal: synthetic vocal, 12 s @ 48 kHz, input %.2f LKFS\n\n", inLkfs);

    // ---------------------------------------------------------------- 1. makeup
    // Measure the loudness the chain loses with makeup disabled, on a grid.
    // The required makeup is exactly the negative of that loss. An analytic
    // fit was tried first and could not hold better than ~2.5 LU across the
    // grid (the character axis is convex and its curvature varies with
    // amount), so the shipped form is this measured table plus bilinear
    // interpolation - 55 floats, evaluated once per block.
    constexpr int kAmountPoints = 11;   // 0,10,...,100
    constexpr int kCharPoints   = 5;    // 0,25,50,75,100

    double table[kAmountPoints][kCharPoints];
    double grTable[kAmountPoints][kCharPoints];

    std::printf ("=== 1. LOUDNESS LOSS (makeup disabled) ===\n");
    std::printf ("%8s %10s %10s %10s %10s %10s\n",
                 "amount", "chr=0", "chr=25", "chr=50", "chr=75", "chr=100");

    for (int ai = 0; ai < kAmountPoints; ++ai)
    {
        const float amount = static_cast<float> (ai) * 10.0f;
        std::printf ("%8.0f", amount);

        for (int ci = 0; ci < kCharPoints; ++ci)
        {
            const float chr = static_cast<float> (ci) * 25.0f;
            const auto res = runChain (vocal, sr, amount, chr, 85.0f, false);
            const double loss = measureLkfs (res.output) - inLkfs;
            table[ai][ci]   = -loss;               // makeup needed, in dB
            grTable[ai][ci] = res.meanTotalGr;     // mean GR it was measured against
            std::printf (" %10.3f", loss);
        }
        std::printf ("\n");
    }

    std::printf ("\n--- paste into MacroMap.h ---\n");
    std::printf ("static constexpr float kMakeupTable[%d][%d] =\n{\n", kAmountPoints, kCharPoints);
    for (int ai = 0; ai < kAmountPoints; ++ai)
    {
        std::printf ("    { ");
        for (int ci = 0; ci < kCharPoints; ++ci)
            std::printf ("%7.3ff%s", table[ai][ci], ci < kCharPoints - 1 ? ", " : "");
        std::printf (" },   // amount %3d%%\n", ai * 10);
    }
    std::printf ("};\n\n");

    std::printf ("static constexpr float kExpectedGrTable[%d][%d] =\n{\n", kAmountPoints, kCharPoints);
    for (int ai = 0; ai < kAmountPoints; ++ai)
    {
        std::printf ("    { ");
        for (int ci = 0; ci < kCharPoints; ++ci)
            std::printf ("%7.3ff%s", grTable[ai][ci], ci < kCharPoints - 1 ? ", " : "");
        std::printf (" },   // amount %3d%%\n", ai * 10);
    }
    std::printf ("};\n");

    // ------------------------------------------------------------ 2. continuity
    std::printf ("\n=== 2. CHARACTER MORPH CONTINUITY (amount=60) ===\n");
    std::printf ("Largest LKFS step between adjacent character settings:\n");

    double prev = 0.0;
    double worstStep = 0.0;
    int worstAt = 0;

    for (int ch = 0; ch <= 100; ch += 2)
    {
        const auto res = runChain (vocal, sr, 60.0f, static_cast<float> (ch), 85.0f, true);
        const double lk = measureLkfs (res.output);

        if (ch > 0)
        {
            const double step = std::abs (lk - prev);
            if (step > worstStep) { worstStep = step; worstAt = ch; }
        }
        prev = lk;
    }
    std::printf ("  worst step: %.4f LU at character=%d (threshold for audible jump ~0.5 LU)\n",
                 worstStep, worstAt);

    // -------------------------------------------------------------- 3. aliasing
    std::printf ("\n=== 3. ALIASING PROBE (7 kHz sine, -3 dBFS, 44.1 kHz) ===\n");
    const double srAlias = 44100.0;
    const auto sine = makeSine (srAlias, 2.0, 7000.0, -3.0);
    const auto aliased = runChain (sine, srAlias, 85.0f, 100.0f, 85.0f, true);

    const double fund = goertzel (aliased.output, 7000.0, srAlias);
    struct Probe { const char* name; double hz; };
    const Probe probes[] = {
        { "h2  14.0k (in band)", 14000.0 },
        { "h3  21.0k (in band)", 21000.0 },
        { "h4->16.1k (ALIAS)",   16100.0 },
        { "h5-> 9.1k (ALIAS)",    9100.0 },
        { "h6-> 2.1k (ALIAS)",    2100.0 }
    };

    for (const auto& p : probes)
    {
        const double mag = goertzel (aliased.output, p.hz, srAlias);
        std::printf ("  %-22s %8.2f dB rel. fundamental\n",
                     p.name, 20.0 * std::log10 (std::max (mag / std::max (fund, 1e-12), 1e-12)));
    }

    // ---------------------------------------------- 4. makeup residual (exit gate)
    std::printf ("\n=== 4. LOUDNESS HOLD WITH MAKEUP ENABLED (exit criterion: within 1 LU) ===\n");
    std::printf ("%8s %10s %10s %10s %10s %10s\n",
                 "amount", "chr=0", "chr=25", "chr=50", "chr=75", "chr=100");

    double worstResidual = 0.0;
    int worstA = 0, worstC = 0;

    for (int a = 0; a <= 100; a += 5)         // 5% steps: lands between table rows
    {
        std::printf ("%8d", a);
        for (int ci = 0; ci < 5; ++ci)
        {
            const float chr = static_cast<float> (ci) * 25.0f;
            const auto res = runChain (vocal, sr, static_cast<float> (a), chr, 85.0f, true);
            const double residual = measureLkfs (res.output) - inLkfs;
            std::printf (" %10.3f", residual);

            if (std::abs (residual) > std::abs (worstResidual))
            {
                worstResidual = residual;
                worstA = a;
                worstC = static_cast<int> (chr);
            }
        }
        std::printf ("\n");
    }

    std::printf ("\n  worst residual: %+.3f LU at amount=%d%%, character=%d%%  -> %s\n",
                 worstResidual, worstA, worstC,
                 std::abs (worstResidual) <= 1.0 ? "PASS" : "FAIL");

    // ------------------------------------------- 5. opto release program dependence
    std::printf ("\n=== 5. OPTO RELEASE PROGRAM DEPENDENCE ===\n");
    std::printf ("Recovery to within 1 dB of unity after a 1 s tone burst:\n");

    for (float burstDb : { -6.0f, -1.0f })
    {
        const size_t burst = static_cast<size_t> (sr * 1.0);
        const size_t tail  = static_cast<size_t> (sr * 4.0);
        std::vector<float> sig (burst + tail, 0.0f);

        const double amp = std::pow (10.0, burstDb / 20.0);
        for (size_t i = 0; i < burst; ++i)
            sig[i] = static_cast<float> (amp * std::sin (2.0 * kPi * 220.0 * i / sr));
        for (size_t i = burst; i < sig.size(); ++i)
            sig[i] = static_cast<float> (0.02 * std::sin (2.0 * kPi * 220.0 * i / sr));

        // instrument the opto stage directly
        const vrs::MacroCoefficients c = vrs::mapMacros (0.7f, 0.0f);
        vrs::OptoStage opto;
        TptHighpass hpf;
        opto.prepare (sr);
        hpf.prepare (sr);
        hpf.setCutoff (85.0f);
        opto.setCoefficients (c.optoThresholdDb, c.optoRatio, c.optoKneeDb,
                              c.optoAttackMs, c.optoReleaseFastMs, c.optoReleaseSlowMs);

        float peakGr = 0.0f;
        size_t recoverAt = 0;
        for (size_t i = 0; i < sig.size(); ++i)
        {
            const float gr = opto.processSample (hpf.process (sig[i])) * c.optoWeight;
            if (i < burst) peakGr = std::max (peakGr, gr);
            if (i > burst && recoverAt == 0 && gr < 1.0f) recoverAt = i - burst;
        }

        std::printf ("  burst %+.0f dBFS -> peak GR %5.2f dB, recovery %6.0f ms\n",
                     burstDb, peakGr,
                     recoverAt > 0 ? 1000.0 * static_cast<double> (recoverAt) / sr : -1.0);
    }

    // ------------------------------------------------------ 6. DSP core edge cases
    std::printf ("\n=== 6. DSP CORE EDGE CASES ===\n");

    struct Case { const char* name; float amount; float chr; };
    const Case cases[] = {
        { "amount=0   (unity)",   0.0f,   0.0f },
        { "amount=50  chr=50",   50.0f,  50.0f },
        { "amount=100 chr=100", 100.0f, 100.0f }
    };

    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    bool allFinite = true;
    bool silenceClean = true;

    for (double rate : rates)
    {
        // silence in -> silence out, and nothing must go non-finite
        std::vector<float> silence (static_cast<size_t> (rate * 0.5), 0.0f);
        // DC: the classic denormal / integrator-windup trap
        std::vector<float> dc (static_cast<size_t> (rate * 0.5), 0.5f);
        // full scale
        std::vector<float> hot = makeSine (rate, 0.5, 1000.0, 0.0);
        // tiny: 1e-30 is deep in denormal territory
        std::vector<float> tiny (static_cast<size_t> (rate * 0.1), 1e-30f);
        // impulse into silence
        std::vector<float> impulse (static_cast<size_t> (rate * 0.5), 0.0f);
        impulse[100] = 1.0f;

        struct Sig { const char* name; const std::vector<float>* data; };
        const Sig sigs[] = {
            { "silence", &silence }, { "DC 0.5", &dc }, { "0 dBFS sine", &hot },
            { "1e-30",   &tiny },    { "impulse", &impulse }
        };

        for (const auto& c : cases)
        {
            for (const auto& sig : sigs)
            {
                const auto res = runChain (*sig.data, rate, c.amount, c.chr, 85.0f, true);

                float peak = 0.0f;
                for (float v : res.output)
                {
                    if (! std::isfinite (v)) { allFinite = false; }
                    peak = std::max (peak, std::abs (v));
                }

                if (std::string (sig.name) == "silence" && peak > 1e-12f)
                    silenceClean = false;

                // report only the interesting rows
                if (peak > 4.0f)
                    std::printf ("  !! %6.0f Hz %-22s %-12s peak %.3f\n",
                                 rate, c.name, sig.name, peak);
            }
        }
    }

    std::printf ("  all outputs finite (no NaN/Inf):        %s\n", allFinite ? "PASS" : "FAIL");
    std::printf ("  silence in -> silence out:              %s\n", silenceClean ? "PASS" : "FAIL");
    std::printf ("  no output exceeded +12 dBFS on any case: (rows above, if any)\n");

    // ------------------------------------------------- 7. automation thrash safety
    std::printf ("\n=== 7. FAST AUTOMATION (per-block macro jumps) ===\n");
    {
        const size_t blockLen = 64;
        std::mt19937 rng { 7 };
        std::uniform_real_distribution<float> uni { 0.0f, 100.0f };

        vrs::OptoStage opto; vrs::FetStage fet; TptHighpass hpf;
        opto.prepare (sr); fet.prepare (sr); hpf.prepare (sr); hpf.setCutoff (85.0f);

        float worstJump = 0.0f, prevOut = 0.0f;
        bool finite = true;

        for (size_t b = 0; b * blockLen < vocal.size(); ++b)
        {
            const vrs::MacroCoefficients c = vrs::mapMacros (uni (rng) * 0.01f, uni (rng) * 0.01f);
            opto.setCoefficients (c.optoThresholdDb, c.optoRatio, c.optoKneeDb,
                                  c.optoAttackMs, c.optoReleaseFastMs, c.optoReleaseSlowMs);
            fet.setCoefficients  (c.fetThresholdDb, c.fetRatio, c.fetKneeDb,
                                  c.fetAttackMs, c.fetReleaseMs);

            for (size_t i = b * blockLen; i < std::min (vocal.size(), (b + 1) * blockLen); ++i)
            {
                const float d  = hpf.process (vocal[i]);
                const float og = vrs::dbToGain (-(opto.processSample (d) * c.optoWeight));
                const float fg = vrs::dbToGain (-(fet.processSample (d * og) * c.fetWeight));
                const float y  = vocal[i] * og * fg * vrs::dbToGain (c.makeupDb);

                if (! std::isfinite (y)) finite = false;
                worstJump = std::max (worstJump, std::abs (y - prevOut));
                prevOut = y;
            }
        }

        std::printf ("  randomised macros every 64 samples, finite output: %s\n",
                     finite ? "PASS" : "FAIL");
        std::printf ("  largest sample-to-sample jump: %.4f (signal peak ~%.2f)\n",
                     worstJump, 0.5f);
        std::printf ("  NOTE: the engine additionally smooths the macros over 20 ms;\n");
        std::printf ("        this probe deliberately runs UNSMOOTHED as a worst case.\n");
    }

    // ------------------------------------------- 8. makeup table fixed-point step
    // Activity scaling is clamped at 1, so its average over programme material
    // sits below 1 and the table under-compensates. Rather than removing the
    // clamp (which would let transients boost the makeup), fold the measured
    // residual back into the table and iterate.
    std::printf ("\n=== 8. CORRECTED MAKEUP TABLE (one fixed-point iteration) ===\n");
    std::printf ("static constexpr float kMakeupTable[%d][%d] =\n{\n", kAmountPoints, kCharPoints);

    for (int ai = 0; ai < kAmountPoints; ++ai)
    {
        std::printf ("    { ");
        for (int ci = 0; ci < kCharPoints; ++ci)
        {
            const float amount = static_cast<float> (ai) * 10.0f;
            const float chr    = static_cast<float> (ci) * 25.0f;

            const auto res = runChain (vocal, sr, amount, chr, 85.0f, true);
            const double residual = measureLkfs (res.output) - inLkfs;
            const double current  = vrs::lookupMakeupDb (amount * 0.01f, chr * 0.01f);
            const double corrected = (current <= 0.0001 && residual > -0.0001)
                                   ? 0.0 : current - residual;

            std::printf ("%7.3ff%s", corrected, ci < kCharPoints - 1 ? ", " : "");
        }
        std::printf (" },   // amount %3d%%\n", ai * 10);
    }
    std::printf ("};\n");

    // ================================================ 9. MANUAL MODE IS REAL
    std::printf ("\n=== 9. MANUAL MODE - are the four knobs actually wired? ===\n");
    std::printf ("(mean gain reduction in dB on the vocal signal; CHARACTER fixed at 50)\n\n");

    std::printf ("  THRESHOLD sweep  (ratio 4:1, atk 10 ms, rel 250 ms)\n");
    for (float th : { -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -36.0f })
        std::printf ("    %6.0f dB -> mean GR %6.2f dB   peak %6.2f dB\n", th,
                     runChainManual (vocal, sr, th, 4.0f, 10.0f, 250.0f, 50.0f).meanTotalGr,
                     runChainManual (vocal, sr, th, 4.0f, 10.0f, 250.0f, 50.0f).peakOptoGr
                   + runChainManual (vocal, sr, th, 4.0f, 10.0f, 250.0f, 50.0f).peakFetGr);

    std::printf ("\n  RATIO sweep      (thr -24 dB, atk 10 ms, rel 250 ms)\n");
    for (float rt : { 1.5f, 2.0f, 4.0f, 8.0f, 12.0f, 20.0f })
        std::printf ("    %6.1f:1 -> mean GR %6.2f dB\n", rt,
                     runChainManual (vocal, sr, -24.0f, rt, 10.0f, 250.0f, 50.0f).meanTotalGr);

    std::printf ("\n  ATTACK sweep     (thr -24 dB, ratio 8:1, rel 250 ms)\n");
    for (float at : { 0.1f, 1.0f, 5.0f, 20.0f, 50.0f, 100.0f })
        std::printf ("    %6.1f ms -> mean GR %6.2f dB\n", at,
                     runChainManual (vocal, sr, -24.0f, 8.0f, at, 250.0f, 50.0f).meanTotalGr);

    std::printf ("\n  RELEASE sweep    (thr -24 dB, ratio 8:1, atk 10 ms)\n");
    for (float rl : { 20.0f, 60.0f, 150.0f, 400.0f, 1000.0f, 2000.0f })
        std::printf ("    %6.0f ms -> mean GR %6.2f dB\n", rl,
                     runChainManual (vocal, sr, -24.0f, 8.0f, 10.0f, rl, 50.0f).meanTotalGr);

    return 0;
}
