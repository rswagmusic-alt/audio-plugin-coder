/*
  ==============================================================================

    CompressorStages.h - VictorRSwagVocalComp

    The two dynamics stages from .ideas/architecture.md.

    Both stages are DETECTOR-ONLY: they observe a shared, high-passed mono
    detector signal and return a gain-reduction amount in dB. The engine
    applies that reduction to the audio. Keeping the stages free of the audio
    path makes them trivially stereo-linked and easy to meter.

    Real-time safe: no allocation, no locking, no branching on denormals.

  ==============================================================================
*/

#pragma once

#include <cmath>
#include <algorithm>

namespace vrs
{

/** One-pole coefficient for a given time constant. */
inline float timeConstantCoeff (float timeMs, double sampleRate) noexcept
{
    if (timeMs <= 0.0f)
        return 1.0f;

    return 1.0f - std::exp (-1.0f / (0.001f * timeMs * static_cast<float> (sampleRate)));
}

inline float gainToDb (float gain) noexcept
{
    return 20.0f * std::log10 (std::max (gain, 1.0e-6f));
}

inline float dbToGain (float db) noexcept
{
    return std::pow (10.0f, db * 0.05f);
}

/**
    Soft-knee gain computer, log domain.

    Standard formulation: below the knee the signal passes untouched, inside
    the knee the curve is quadratic, above it the slope is 1/ratio.

    @returns gain reduction in dB, always >= 0
*/
inline float computeGainReductionDb (float levelDb, float thresholdDb,
                                     float ratio, float kneeDb) noexcept
{
    const float over = levelDb - thresholdDb;

    if (over <= -kneeDb * 0.5f)
        return 0.0f;

    const float slope = 1.0f - 1.0f / ratio;

    if (over >= kneeDb * 0.5f)
        return slope * over;

    // inside the knee: quadratic interpolation
    const float x = over + kneeDb * 0.5f;
    return slope * (x * x) / (2.0f * kneeDb);
}


//==============================================================================
/**
    Asymmetric soft saturation for the FET stage.

    Positive and negative halves are shaped differently, which produces even
    harmonics as well as odd - the "richer" quality the brief asks for, rather
    than the hollow sound of a symmetric clipper. The static DC that asymmetry
    would otherwise introduce is subtracted out.

    Drive is scaled by instantaneous gain reduction by the caller, so the
    colour tracks how hard the stage is working.
*/
inline float saturateAsymmetric (float x, float drive) noexcept
{
    if (drive <= 1.0e-4f)
        return x;

    const float k    = 1.0f + drive * 2.5f;
    const float bias = drive * 0.18f;
    const float dc   = std::tanh (bias);

    const float y = std::tanh (k * x + bias) - dc;

    // Partially restore level: tanh compresses, and we do not want the
    // saturator to fight the makeup curve.
    return y * (1.0f + drive * 0.55f) / k;
}

//==============================================================================
/**
    Stage 1 - optical leveller.

    RMS detection over a short window, soft knee, low ratio, and a dual-stage
    program-dependent release: a fast initial recovery that hands over to a
    slow tail as gain reduction deepens. That program dependence is the
    defining characteristic of optical behaviour and is why this stage cannot
    be a plain one-pole follower.
*/
class OptoStage
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        rmsCoeff   = timeConstantCoeff (kRmsWindowMs, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        meanSquare  = 0.0f;
        currentGrDb = 0.0f;
    }

    /** Recomputes ballistics. Call once per block, not per sample. */
    void setCoefficients (float thresholdDb, float ratio, float kneeDb,
                          float attackMs, float releaseFastMs, float releaseSlowMs) noexcept
    {
        threshold  = thresholdDb;
        ratioValue = ratio;
        knee       = kneeDb;

        attackCoeff      = timeConstantCoeff (attackMs,      sampleRate);
        releaseFastCoeff = timeConstantCoeff (releaseFastMs, sampleRate);
        releaseSlowCoeff = timeConstantCoeff (releaseSlowMs, sampleRate);
    }

    /** @returns gain reduction in dB for this sample (>= 0). */
    float processSample (float detector) noexcept
    {
        meanSquare += (detector * detector - meanSquare) * rmsCoeff;

        const float levelDb = gainToDb (std::sqrt (std::max (meanSquare, 0.0f)));
        const float targetGr = computeGainReductionDb (levelDb, threshold, ratioValue, knee);

        if (targetGr > currentGrDb)
        {
            currentGrDb += (targetGr - currentGrDb) * attackCoeff;
        }
        else
        {
            // Program dependence: the deeper the reduction, the more the slow
            // tail dominates. Light touches recover quickly; heavy passages
            // release slowly, which is what keeps sustained vocals even.
            const float depth = std::clamp (currentGrDb / kDepthReferenceDb, 0.0f, 1.0f);
            const float coeff = releaseFastCoeff + (releaseSlowCoeff - releaseFastCoeff) * depth;
            currentGrDb += (targetGr - currentGrDb) * coeff;
        }

        return currentGrDb;
    }

    float getGainReductionDb() const noexcept { return currentGrDb; }

private:
    static constexpr float kRmsWindowMs      = 10.0f;
    static constexpr float kDepthReferenceDb = 8.0f;

    double sampleRate = 44100.0;

    float threshold  = 0.0f;
    float ratioValue = 1.5f;
    float knee       = 12.0f;

    float rmsCoeff         = 0.0f;
    float attackCoeff      = 0.0f;
    float releaseFastCoeff = 0.0f;
    float releaseSlowCoeff = 0.0f;

    float meanSquare  = 0.0f;
    float currentGrDb = 0.0f;
};

//==============================================================================
/**
    Stage 2 - FET peak compressor.

    Peak detection with a very fast attack, harder knee and high ratio. Its job
    is the consonants, plosive edges and shouted syllables that the opto stage
    is too slow to see - so it should be catching transients, not levelling.
*/
class FetStage
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() noexcept
    {
        peakEnv     = 0.0f;
        currentGrDb = 0.0f;
    }

    void setCoefficients (float thresholdDb, float ratio, float kneeDb,
                          float attackMs, float releaseMs) noexcept
    {
        threshold  = thresholdDb;
        ratioValue = ratio;
        knee       = kneeDb;

        attackCoeff  = timeConstantCoeff (attackMs,  sampleRate);
        releaseCoeff = timeConstantCoeff (releaseMs, sampleRate);
        peakCoeff    = timeConstantCoeff (kPeakDecayMs, sampleRate);
    }

    /** @returns gain reduction in dB for this sample (>= 0). */
    float processSample (float detector) noexcept
    {
        const float rectified = std::abs (detector);

        if (rectified > peakEnv)
            peakEnv = rectified;                      // instantaneous peak capture
        else
            peakEnv += (rectified - peakEnv) * peakCoeff;

        const float levelDb  = gainToDb (peakEnv);
        const float targetGr = computeGainReductionDb (levelDb, threshold, ratioValue, knee);

        const float coeff = (targetGr > currentGrDb) ? attackCoeff : releaseCoeff;
        currentGrDb += (targetGr - currentGrDb) * coeff;

        return currentGrDb;
    }

    float getGainReductionDb() const noexcept { return currentGrDb; }

private:
    static constexpr float kPeakDecayMs = 20.0f;

    double sampleRate = 44100.0;

    float threshold  = -6.0f;
    float ratioValue = 4.0f;
    float knee       = 3.0f;

    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;
    float peakCoeff    = 0.0f;

    float peakEnv     = 0.0f;
    float currentGrDb = 0.0f;
};

} // namespace vrs
