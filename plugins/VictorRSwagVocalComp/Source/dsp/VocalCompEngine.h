/*
  ==============================================================================

    VocalCompEngine.h - VictorRSwagVocalComp

    Orchestrates the signal chain from .ideas/architecture.md:

      dry tap -> delay match -------------------------------------\
      input ---> sidechain HPF (detector only) -> opto -> FET ----> mix -> output

    The detector path is mono (channel sum), so both stages are stereo-linked -
    correct for vocals, and it halves the detector cost.

    Real-time safe: everything is sized in prepare(), nothing allocates in
    process().

  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>

#include "MacroMap.h"
#include "CompressorStages.h"

namespace vrs
{

class VocalCompEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate  = spec.sampleRate;
        numChannels = static_cast<int> (spec.numChannels);

        opto.prepare (sampleRate);
        fet.prepare  (sampleRate);

        scFilter.prepare (spec);
        scFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        scFilter.setResonance (0.707f);

        maxBlockSamples = static_cast<int> (spec.maximumBlockSize);
        dryBuffer.setSize (numChannels, maxBlockSamples, false, false, true);

        dryDelay.setMaximumDelayInSamples (kMaxLatencySamples);
        dryDelay.prepare (spec);
        dryDelay.setDelay (static_cast<float> (latencySamples));
        dryDelay.reset();

        detectorBuffer.resize (static_cast<size_t> (spec.maximumBlockSize));

        const double smoothSeconds = 0.02;
        smoothedThreshold.reset (sampleRate, smoothSeconds);
        smoothedRatio    .reset (sampleRate, smoothSeconds);
        smoothedAttack   .reset (sampleRate, smoothSeconds);
        smoothedRelease  .reset (sampleRate, smoothSeconds);
        smoothedAmount   .reset (sampleRate, smoothSeconds);
        smoothedCharacter.reset (sampleRate, smoothSeconds);
        smoothedMix      .reset (sampleRate, smoothSeconds);
        smoothedMakeup   .reset (sampleRate, smoothSeconds);
        smoothedOutput   .reset (sampleRate, smoothSeconds);

        reset();
    }

    void reset()
    {
        opto.reset();
        fet.reset();
        scFilter.reset();
        dryDelay.reset();
        dryBuffer.clear();

        slowGrDb = 0.0f;

        grOptoDb.store (0.0f, std::memory_order_relaxed);
        grFetDb .store (0.0f, std::memory_order_relaxed);
    }

    /** Snaps every smoothed value to its target - use after a preset recall. */
    void snapToTargets()
    {
        smoothedAmount   .setCurrentAndTargetValue (smoothedAmount   .getTargetValue());
        smoothedCharacter.setCurrentAndTargetValue (smoothedCharacter.getTargetValue());
        smoothedMix      .setCurrentAndTargetValue (smoothedMix      .getTargetValue());
        smoothedMakeup   .setCurrentAndTargetValue (smoothedMakeup   .getTargetValue());
        smoothedThreshold.setCurrentAndTargetValue (smoothedThreshold.getTargetValue());
        smoothedRatio    .setCurrentAndTargetValue (smoothedRatio    .getTargetValue());
        smoothedAttack   .setCurrentAndTargetValue (smoothedAttack   .getTargetValue());
        smoothedRelease  .setCurrentAndTargetValue (smoothedRelease  .getTargetValue());
        smoothedOutput   .setCurrentAndTargetValue (smoothedOutput   .getTargetValue());
    }

    /** Control-rate parameter push. Call once per block, before process(). */
    void setParameters (float amountPercent, float characterPercent, float mixPercent,
                        float outputDb, float sidechainHz)
    {
        smoothedAmount   .setTargetValue (amountPercent    * 0.01f);
        smoothedCharacter.setTargetValue (characterPercent * 0.01f);
        smoothedMix      .setTargetValue (mixPercent       * 0.01f);
        smoothedOutput   .setTargetValue (juce::Decibels::decibelsToGain (outputDb));

        scFilter.setCutoffFrequency (juce::jlimit (20.0f, 300.0f, sidechainHz));
    }

    /** MANUAL dynamics section. When manualOn is false these are ignored and
        the macro mapping drives everything exactly as before - the measured
        calibration tables stay in charge. */
    void setManualParameters (bool manualOn, float thresholdDb, float ratioValue,
                              float attackMs, float releaseMs)
    {
        manualMode = manualOn;
        smoothedThreshold.setTargetValue (thresholdDb);
        smoothedRatio    .setTargetValue (ratioValue);
        smoothedAttack   .setTargetValue (attackMs);
        smoothedRelease  .setTargetValue (releaseMs);
    }

    /** Handles any block length. Hosts are supposed to respect the size
        declared in prepareToPlay(), but not all of them do, and allocating a
        bigger scratch buffer on the audio thread is not an option - so
        oversized blocks are processed in chunks instead. */
    void process (juce::AudioBuffer<float>& buffer)
    {
        const int totalSamples = buffer.getNumSamples();
        const int activeChans  = juce::jmin (numChannels, buffer.getNumChannels());

        if (totalSamples <= 0 || activeChans <= 0 || maxBlockSamples <= 0)
            return;

        for (int offset = 0; offset < totalSamples; offset += maxBlockSamples)
            processChunk (buffer, offset, juce::jmin (maxBlockSamples, totalSamples - offset),
                          activeChans);
    }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                       int activeChans)
    {

        // ---- control rate: resolve macros once per block ---------------------
        smoothedAmount.skip (numSamples - 1);
        smoothedCharacter.skip (numSamples - 1);

        const float amountN    = smoothedAmount.getNextValue();
        const float characterN = smoothedCharacter.getNextValue();

        MacroCoefficients c = mapMacros (amountN, characterN);

        if (manualMode)
            applyManualOverride (c, numSamples);

        opto.setCoefficients (c.optoThresholdDb, c.optoRatio, c.optoKneeDb,
                              c.optoAttackMs, c.optoReleaseFastMs, c.optoReleaseSlowMs);
        fet.setCoefficients  (c.fetThresholdDb, c.fetRatio, c.fetKneeDb,
                              c.fetAttackMs, c.fetReleaseMs);

        // Activity-scaled makeup.
        //
        // The makeup table is calibrated for the mean gain reduction this macro
        // setting produces on real programme material. When the chain is NOT
        // actually compressing - DC, sub-sonic rumble, a lone impulse, anything
        // the sidechain HPF hides from the detector - applying the full table
        // value is a pure boost with nothing to pay for it. Scaling by a slow
        // average of the reduction actually achieved keeps the calibration
        // intact on programme material and collapses the boost to zero when
        // nothing is being compressed.
        //
        // The average is slow (400 ms) on purpose: tracking instantaneous GR
        // would raise makeup exactly when reduction peaks, partially undoing
        // the compression and lowering the effective ratio.
        const float activity = (c.expectedGrDb > kActivityFloorDb)
                             ? std::clamp (slowGrDb / c.expectedGrDb, 0.0f, 1.0f)
                             : 0.0f;

        smoothedMakeup.setTargetValue (
            juce::Decibels::decibelsToGain (c.makeupDb * activity));

        slowGrCoeff = 1.0f - std::exp (-1.0f / (0.001f * kActivityTimeMs
                                                * static_cast<float> (sampleRate)));

        // ---- dry tap ---------------------------------------------------------
        for (int ch = 0; ch < activeChans; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, startSample, numSamples);

        // ---- detector: mono sum, high-passed ---------------------------------
        const float channelScale = 1.0f / static_cast<float> (activeChans);

        for (int n = 0; n < numSamples; ++n)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < activeChans; ++ch)
                sum += buffer.getSample (ch, startSample + n);

            detectorBuffer[static_cast<size_t> (n)] =
                scFilter.processSample (0, sum * channelScale);
        }

        // ---- two stages in series --------------------------------------------
        float lastOptoGr = 0.0f;
        float lastFetGr  = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            const float detector = detectorBuffer[static_cast<size_t> (n)];

            // Stage 1 sees the raw detector.
            const float optoGrDb = opto.processSample (detector) * c.optoWeight;
            const float optoGain = dbToGain (-optoGrDb);

            // Stage 2 sees what stage 1 left behind - so it only has to catch
            // the transients the opto was too slow for.
            const float fetGrDb = fet.processSample (detector * optoGain) * c.fetWeight;
            const float fetGain = dbToGain (-fetGrDb);

            const float makeup  = smoothedMakeup.getNextValue();
            const float mix     = smoothedMix.getNextValue();
            const float outGain = smoothedOutput.getNextValue();

            // Saturation drive tracks how hard the FET stage is working, so
            // the plugin colours under load and stays clean when idle.
            const float satDrive = c.satDrive
                                 * std::clamp (fetGrDb / kSatReferenceGrDb, 0.0f, 1.0f);

            for (int ch = 0; ch < activeChans; ++ch)
            {
                // Push before pop: with a delay of 0 this must return the
                // sample just written, otherwise the dry path picks up a
                // one-sample offset and the mix=0 null test fails.
                dryDelay.pushSample (ch, dryBuffer.getSample (ch, n));
                const float dry = dryDelay.popSample (ch);

                const float compressed = buffer.getSample (ch, startSample + n) * optoGain * fetGain;
                const float wet = saturateAsymmetric (compressed, satDrive) * makeup;

                // Equal-gain blend: dry and wet are correlated, so equal-power
                // would overshoot in the middle of the sweep.
                // At mix = 0 this reduces to dry * outGain exactly, which is
                // what makes the null test against bypass meaningful.
                buffer.setSample (ch, startSample + n, (dry * (1.0f - mix) + wet * mix) * outGain);
            }

            slowGrDb += ((optoGrDb + fetGrDb) - slowGrDb) * slowGrCoeff;

            lastOptoGr = optoGrDb;
            lastFetGr  = fetGrDb;
        }

        grOptoDb.store (lastOptoGr, std::memory_order_relaxed);
        grFetDb .store (lastFetGr,  std::memory_order_relaxed);
    }

public:
    float getOptoGrDb() const noexcept { return grOptoDb.load (std::memory_order_relaxed); }
    float getFetGrDb()  const noexcept { return grFetDb .load (std::memory_order_relaxed); }

    /** Zero for now. Phase 2.1.2 may add FET oversampling, which must be
        reported to the host AND pushed into dryDelay, or the parallel mix
        will comb-filter. */
    int getLatencySamples() const noexcept { return latencySamples; }

private:
    static constexpr int   kMaxLatencySamples  = 256;
    static constexpr float kSatReferenceGrDb   = 8.0f;
    static constexpr float kActivityTimeMs     = 400.0f;
    static constexpr float kActivityFloorDb    = 0.05f;

    /** Replaces the macro-derived dynamics coefficients with the user's own.
        Deliberately leaves optoWeight / fetWeight / satDrive untouched, so
        CHARACTER still morphs the opto<->FET balance and the saturation in
        manual mode. The opto keeps its dual-stage program-dependent release -
        the algorithm is unchanged, only where its numbers come from. */
    void applyManualOverride (MacroCoefficients& c, int numSamples) noexcept
    {
        smoothedThreshold.skip (numSamples - 1);
        smoothedRatio    .skip (numSamples - 1);
        smoothedAttack   .skip (numSamples - 1);
        smoothedRelease  .skip (numSamples - 1);

        const float thresholdDb = smoothedThreshold.getNextValue();
        const float ratioValue  = juce::jmax (1.0f, smoothedRatio.getNextValue());
        const float attackMs    = smoothedAttack.getNextValue();
        const float releaseMs   = smoothedRelease.getNextValue();

        // Both stages share the user's values, so every number on screen is
        // literally the coefficient in use - no hidden per-stage fudge.
        c.optoThresholdDb   = thresholdDb;
        c.fetThresholdDb    = thresholdDb;
        c.optoRatio         = ratioValue;
        c.fetRatio          = ratioValue;
        c.optoAttackMs      = attackMs;
        c.fetAttackMs       = attackMs;
        c.optoReleaseFastMs = releaseMs * 0.35f;   // preserves the dual-stage opto
        c.optoReleaseSlowMs = releaseMs;
        c.fetReleaseMs      = releaseMs;

        // The measured makeup tables are indexed by (amount, character) and are
        // meaningless here, so manual mode uses a classic estimate instead.
        // Activity scaling still applies, which keeps the DC / sub-sonic boost
        // guard from Phase 2.1.3 in force.
        const float over  = juce::jmax (0.0f, -thresholdDb);
        const float slope = 1.0f - 1.0f / ratioValue;

        c.makeupDb     = juce::jlimit (0.0f, 24.0f, 0.50f * slope * over);
        c.expectedGrDb = juce::jlimit (0.05f, 24.0f, 0.35f * slope * over);
    }

    double sampleRate  = 44100.0;
    int    numChannels = 2;
    int    latencySamples  = 0;
    int    maxBlockSamples = 0;

    float slowGrDb    = 0.0f;   // 400 ms average of total gain reduction
    float slowGrCoeff = 0.0f;

    bool manualMode = false;
    juce::SmoothedValue<float> smoothedThreshold { -18.0f }, smoothedRatio { 4.0f },
                               smoothedAttack { 10.0f },     smoothedRelease { 250.0f };

    OptoStage opto;
    FetStage  fet;

    juce::dsp::StateVariableTPTFilter<float> scFilter;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { kMaxLatencySamples };

    juce::AudioBuffer<float> dryBuffer;
    std::vector<float>       detectorBuffer;

    juce::SmoothedValue<float> smoothedAmount, smoothedCharacter, smoothedMix,
                               smoothedMakeup, smoothedOutput;

    std::atomic<float> grOptoDb { 0.0f };
    std::atomic<float> grFetDb  { 0.0f };
};

} // namespace vrs
