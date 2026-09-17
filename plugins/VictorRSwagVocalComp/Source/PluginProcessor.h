/*
  ==============================================================================

    PluginProcessor.h - VictorRSwagVocalComp
    "Victor R Swag Vocal Compressor"

    Hybrid opto -> FET vocal compressor driven by a single Amount macro.
    See .ideas/architecture.md for the DSP design.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "ParameterIDs.hpp"
#include "dsp/VocalCompEngine.h"

//==============================================================================
class VictorRSwagVocalCompAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    VictorRSwagVocalCompAudioProcessor();
    ~VictorRSwagVocalCompAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    //==============================================================================
    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    //==============================================================================
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Lets the host drive bypass through its own UI and automation. */
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    /** Gain reduction in dB for the UI meter. Audio thread writes, message
        thread reads - atomics only, never a lock. */
    float getOptoGrDb() const noexcept { return engine.getOptoGrDb(); }
    float getFetGrDb()  const noexcept { return engine.getFetGrDb();  }

    /** Peak levels for the IN/OUT meters, linear 0..1. Metering only - these
        are written by the audio thread and never read by it. */
    float getInLevel()  const noexcept { return inLevel .load (std::memory_order_relaxed); }
    float getOutLevel() const noexcept { return outLevel.load (std::memory_order_relaxed); }

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Cached raw pointers - resolved once at construction. Looking parameters
    // up by string inside processBlock() is a hash lookup per block and is
    // explicitly forbidden by the impl rules.
    std::atomic<float>* amountParam    = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* mixParam       = nullptr;
    std::atomic<float>* outputParam    = nullptr;
    std::atomic<float>* scHpfParam     = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    // MANUAL dynamics section
    juce::AudioParameterBool* manualModeParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* ratioParam     = nullptr;
    std::atomic<float>* attackParam    = nullptr;
    std::atomic<float>* releaseParam   = nullptr;

    vrs::VocalCompEngine engine;

    // Ramped bypass - switching hard would click.
    juce::SmoothedValue<float> bypassRamp;
    juce::AudioBuffer<float>   bypassDryBuffer;
    int maxBlockSamples = 0;

    // Set by setStateInformation() on the message thread, consumed at the top
    // of the next processBlock(). Snapping smoothed values straight from the
    // message thread would race with the audio thread.
    std::atomic<bool> pendingSnap { false };

    std::atomic<float> inLevel  { 0.0f };
    std::atomic<float> outLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VictorRSwagVocalCompAudioProcessor)
};
