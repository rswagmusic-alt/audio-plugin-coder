/*
  ==============================================================================

    MacroMap.h - VictorRSwagVocalComp

    The macro mapping engine from .ideas/architecture.md.

    Two user-facing knobs (amount, character) drive roughly a dozen internal
    coefficients. Centralising that relationship here keeps the compressor
    stages dumb and makes the curves tunable in one place - which matters,
    because Phase 2.1.2 calibrates these by measurement.

    Evaluated at CONTROL RATE (once per block), never per sample.

  ==============================================================================
*/

#pragma once

#include <cmath>
#include <algorithm>

namespace vrs
{

/** Every coefficient the two stages need, derived from the two macros. */
struct MacroCoefficients
{
    // Stage 1 - opto leveller
    float optoThresholdDb   = 0.0f;
    float optoRatio         = 1.5f;
    float optoKneeDb        = 12.0f;   // soft
    float optoAttackMs      = 30.0f;
    float optoReleaseFastMs = 80.0f;
    float optoReleaseSlowMs = 2000.0f;
    float optoWeight        = 1.0f;

    // Stage 2 - FET peak compressor
    float fetThresholdDb    = -6.0f;
    float fetRatio          = 4.0f;
    float fetKneeDb         = 3.0f;    // hard-ish
    float fetAttackMs       = 8.0f;
    float fetReleaseMs      = 250.0f;
    float fetWeight         = 0.2f;
    float satDrive          = 0.0f;    // asymmetric FET saturation, 0..1

    // Loudness compensation
    float makeupDb          = 0.0f;   // full value at the calibrated operating point
    float expectedGrDb      = 0.0f;   // mean GR that value was measured against
};

/** Linear interpolation. */
inline float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

/** Exponential (geometric) interpolation - the right curve for time constants. */
inline float expLerp (float a, float b, float t) noexcept
{
    return a * std::pow (b / a, t);
}


//==============================================================================
// Auto-makeup: measured, not derived.
//
// Values are the loudness (ITU-R BS.1770 K-weighted, LKFS) that the chain
// loses with makeup disabled, negated - i.e. exactly the gain needed to hold
// perceived loudness constant across the amount sweep, which is the brief's
// #1 feel requirement.
//
// Produced by tools/calibrate.cpp. A closed-form (1 - 1/ratio) estimate does
// not work here: two stages in series with independent thresholds do not
// compose that way, and the character axis is convex with curvature that
// itself varies with amount. An analytic fit could not hold better than
// ~2.5 LU across the grid, so this is a measured table with bilinear
// interpolation - 55 floats, evaluated once per block.
//
// These values include one fixed-point iteration against the activity scaling
// below: activity is clamped at 1, so its average over programme material sits
// under 1 and the raw loss figures would under-compensate by up to 1.4 LU.
//
// CAVEAT: measured against the synthetic vocal-like signal in
// tools/calibrate.cpp, not a real vocal recording. Re-run the harness against
// real material before release and regenerate this table.
//==============================================================================
inline constexpr int kMakeupAmountPoints = 11;   // amount 0,10,...,100 %
inline constexpr int kMakeupCharPoints   = 5;    // character 0,25,50,75,100 %

inline constexpr float kMakeupTable[kMakeupAmountPoints][kMakeupCharPoints] =
{
    {   0.000f,   0.000f,   0.000f,   0.000f,   0.000f },   // amount   0%
    {   0.000f,   0.000f,   0.000f,   0.000f,   0.000f },   // amount  10%
    {   0.000f,   0.000f,   0.000f,   0.000f,   0.000f },   // amount  20%
    {   0.130f,   0.644f,   0.694f,   0.724f,   0.754f },   // amount  30%
    {   0.696f,   1.367f,   1.521f,   1.602f,   1.625f },   // amount  40%
    {   1.748f,   2.461f,   2.717f,   2.868f,   2.935f },   // amount  50%
    {   3.364f,   4.248f,   4.720f,   5.010f,   5.237f },   // amount  60%
    {   5.638f,   6.750f,   7.415f,   7.988f,   8.646f },   // amount  70%
    {   9.253f,  10.151f,  10.789f,  11.724f,  13.068f },   // amount  80%
    {  13.021f,  13.646f,  14.402f,  15.885f,  17.991f },   // amount  90%
    {  16.245f,  16.940f,  18.117f,  20.264f,  23.131f },   // amount 100%
};

//==============================================================================
// Mean total gain reduction measured at each grid point, on the same signal.
//
// The engine divides its slow-averaged actual GR by this to get an "activity"
// factor and scales the makeup by it. Without that, material the sidechain HPF
// hides from the detector - DC, sub-sonic rumble, a lone impulse - receives the
// full makeup boost with no compression to pay for it. Measured before the fix:
// a -6 dBFS DC input came out at +15.7 dBFS at amount=100 / character=100.
//==============================================================================
inline constexpr float kExpectedGrTable[kMakeupAmountPoints][kMakeupCharPoints] =
{
    {   0.000f,   0.000f,   0.000f,   0.000f,   0.000f },   // amount   0%
    {   0.000f,   0.000f,   0.000f,   0.000f,   0.000f },   // amount  10%
    {   0.000f,   0.000f,   0.000f,   0.000f,   0.000f },   // amount  20%
    {   0.024f,   0.035f,   0.040f,   0.041f,   0.039f },   // amount  30%
    {   0.170f,   0.226f,   0.253f,   0.260f,   0.255f },   // amount  40%
    {   0.606f,   0.833f,   0.981f,   1.058f,   1.083f },   // amount  50%
    {   1.628f,   2.219f,   2.665f,   2.946f,   3.110f },   // amount  60%
    {   3.423f,   4.424f,   5.220f,   5.780f,   6.178f },   // amount  70%
    {   6.960f,   7.818f,   8.588f,   9.227f,   9.799f },   // amount  80%
    {  11.185f,  11.642f,  12.281f,  13.006f,  13.792f },   // amount  90%
    {  14.276f,  14.934f,  15.857f,  16.892f,  18.043f },   // amount 100%
};

/** Shared bilinear sampler for the two calibration tables. */
inline float sampleTable (const float table[kMakeupAmountPoints][kMakeupCharPoints],
                          float amountN, float characterN) noexcept
{
    const float a = std::clamp (amountN,    0.0f, 1.0f) * (kMakeupAmountPoints - 1);
    const float c = std::clamp (characterN, 0.0f, 1.0f) * (kMakeupCharPoints   - 1);

    const int a0 = std::min (static_cast<int> (a), kMakeupAmountPoints - 2);
    const int c0 = std::min (static_cast<int> (c), kMakeupCharPoints   - 2);

    const float fa = a - static_cast<float> (a0);
    const float fc = c - static_cast<float> (c0);

    const float lower = lerp (table[a0    ][c0], table[a0    ][c0 + 1], fc);
    const float upper = lerp (table[a0 + 1][c0], table[a0 + 1][c0 + 1], fc);

    return lerp (lower, upper, fa);
}

/** Makeup gain at the calibrated operating point, in dB. */
inline float lookupMakeupDb (float amountN, float characterN) noexcept
{
    return sampleTable (kMakeupTable, amountN, characterN);
}

/** Mean gain reduction the makeup table was calibrated against, in dB. */
inline float lookupExpectedGrDb (float amountN, float characterN) noexcept
{
    return sampleTable (kExpectedGrTable, amountN, characterN);
}

/**
    Maps the two macros onto the full coefficient set.

    @param amountN     amount knob, normalised 0..1
    @param characterN  character knob, normalised 0..1 (0 = opto/smooth, 1 = FET/aggressive)

    Curve notes (see .ideas/architecture.md):
      - The opto threshold uses a lower exponent (0.8) than the FET threshold
        (1.2) so the opto stage ENGAGES FIRST and the FET stage follows. That
        ordering is what stops the two stages fighting at high amount settings.
      - Time constants interpolate geometrically, not linearly - a linear sweep
        from 8 ms to 0.1 ms spends almost all its travel in the inaudible range.
*/
inline MacroCoefficients mapMacros (float amountN, float characterN) noexcept
{
    amountN    = std::clamp (amountN,    0.0f, 1.0f);
    characterN = std::clamp (characterN, 0.0f, 1.0f);

    MacroCoefficients c;

    // ---- thresholds: opto leads, FET follows -------------------------------
    // Both start ABOVE 0 dBFS so that amount=0 is genuinely unity - the spec
    // requires "both stages effectively inactive" there, and measurement showed
    // a 0 dB opto threshold still caught peaks (-0.3 to -1.4 LU at amount=0).
    // The lower opto exponent (0.8 vs 1.2) is what makes the opto ENGAGE FIRST.
    c.optoThresholdDb = 10.0f - std::pow (amountN, 0.8f) * 40.0f;   // +10 .. -30 dB
    c.fetThresholdDb  = 10.0f - std::pow (amountN, 1.2f) * 44.0f;   // +10 .. -34 dB

    // ---- ratios ------------------------------------------------------------
    c.optoRatio = 1.5f + std::pow (amountN, 1.5f) *  2.5f;          // 1.5:1 ..  4:1
    c.fetRatio  = 4.0f + std::pow (amountN, 1.3f) * 16.0f;          //   4:1 .. 20:1

    // ---- timing: character morphs smooth -> aggressive ---------------------
    c.optoAttackMs      = lerp    (30.0f,   10.0f, characterN);
    c.optoReleaseFastMs = 80.0f;                                     // constant
    c.optoReleaseSlowMs = lerp    (2000.0f, 600.0f, characterN);
    c.fetAttackMs       = expLerp (8.0f,    0.1f,  characterN);
    c.fetReleaseMs      = expLerp (250.0f,  40.0f, characterN);

    // ---- stage balance: continuous crossfade, never a switch ---------------
    c.optoWeight = lerp (1.0f, 0.35f, characterN);
    c.fetWeight  = lerp (0.2f, 1.0f,  characterN);

    // ---- saturation --------------------------------------------------------
    // Character alone sets the available drive; the engine scales it further by
    // instantaneous gain reduction, so the plugin only colours when it is
    // actually working ("richer when working harder", per the brief).
    c.satDrive = characterN;

    // ---- auto-makeup -------------------------------------------------------
    c.makeupDb     = lookupMakeupDb (amountN, characterN);
    c.expectedGrDb = lookupExpectedGrDb (amountN, characterN);

    return c;
}

} // namespace vrs
