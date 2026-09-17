/*
  ==============================================================================

    PluginProcessor.cpp - VictorRSwagVocalComp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VictorRSwagVocalCompAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Ranges and defaults come straight from .ideas/parameter-spec.md.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParameterIDs::amount, 1 }, "Amount",
        NormalisableRange<float> (0.0f, 100.0f, 0.01f), 35.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParameterIDs::character, 1 }, "Character",
        NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParameterIDs::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParameterIDs::output, 1 }, "Output",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Skew 0.4377 puts the geometric midpoint (~77 Hz) at the halfway point of
    // the knob, giving a log feel. The UI reads this range back over the
    // interop layer, so the taper is defined here and nowhere else.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParameterIDs::scHpf, 1 }, "Sidechain HPF",
        NormalisableRange<float> (20.0f, 300.0f, 0.01f, 0.4377f), 85.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParameterIDs::bypass, 1 }, "Bypass", false));

    // ---- MANUAL dynamics section ----------------------------------------
    // Off by default, so an existing session recalls exactly as before.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParameterIDs::manualMode, 1 }, "Manual Mode", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParameterIDs::threshold, 1 }, "Threshold",
        NormalisableRange<float> (-48.0f, 0.0f, 0.01f), -18.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Skews place the geometric midpoint at knob centre, so these feel
    // logarithmic under the hand the way hardware does. The UI reads the range
    // back over the interop layer, so the taper is defined here and only here.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParameterIDs::ratio, 1 }, "Ratio",
        NormalisableRange<float> (1.0f, 20.0f, 0.01f, 0.40763f), 4.0f,
        AudioParameterFloatAttributes().withLabel (":1")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParameterIDs::attack, 1 }, "Attack",
        NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.19886f), 10.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParameterIDs::release, 1 }, "Release",
        NormalisableRange<float> (20.0f, 2000.0f, 0.1f, 0.28907f), 250.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));

    return layout;
}

//==============================================================================
VictorRSwagVocalCompAudioProcessor::VictorRSwagVocalCompAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    amountParam    = apvts.getRawParameterValue (ParameterIDs::amount);
    characterParam = apvts.getRawParameterValue (ParameterIDs::character);
    mixParam       = apvts.getRawParameterValue (ParameterIDs::mix);
    outputParam    = apvts.getRawParameterValue (ParameterIDs::output);
    scHpfParam     = apvts.getRawParameterValue (ParameterIDs::scHpf);
    bypassParam    = dynamic_cast<juce::AudioParameterBool*> (
                         apvts.getParameter (ParameterIDs::bypass));

    manualModeParam = dynamic_cast<juce::AudioParameterBool*> (
                          apvts.getParameter (ParameterIDs::manualMode));
    thresholdParam = apvts.getRawParameterValue (ParameterIDs::threshold);
    ratioParam     = apvts.getRawParameterValue (ParameterIDs::ratio);
    attackParam    = apvts.getRawParameterValue (ParameterIDs::attack);
    releaseParam   = apvts.getRawParameterValue (ParameterIDs::release);

    jassert (amountParam != nullptr && characterParam != nullptr && mixParam != nullptr
             && outputParam != nullptr && scHpfParam != nullptr && bypassParam != nullptr);
    jassert (manualModeParam != nullptr && thresholdParam != nullptr && ratioParam != nullptr
             && attackParam != nullptr && releaseParam != nullptr);
}

VictorRSwagVocalCompAudioProcessor::~VictorRSwagVocalCompAudioProcessor() = default;

//==============================================================================
void VictorRSwagVocalCompAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock));
    spec.numChannels      = static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels()));

    engine.prepare (spec);

    maxBlockSamples = juce::jmax (1, samplesPerBlock);
    bypassDryBuffer.setSize (static_cast<int> (spec.numChannels), maxBlockSamples,
                             false, false, true);
    bypassDryBuffer.clear();

    bypassRamp.reset (sampleRate, 0.008);   // 8 ms, click-free
    bypassRamp.setCurrentAndTargetValue (bypassParam->get() ? 0.0f : 1.0f);

    setLatencySamples (engine.getLatencySamples());
}

void VictorRSwagVocalCompAudioProcessor::releaseResources()
{
    engine.reset();
}

void VictorRSwagVocalCompAudioProcessor::reset()
{
    engine.reset();
    bypassRamp.setCurrentAndTargetValue (bypassParam->get() ? 0.0f : 1.0f);
}

bool VictorRSwagVocalCompAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

//==============================================================================
void VictorRSwagVocalCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                       juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numIn       = getTotalNumInputChannels();
    const int numOut      = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples <= 0 || numOut <= 0)
        return;

    engine.setParameters (amountParam->load(),
                          characterParam->load(),
                          mixParam->load(),
                          outputParam->load(),
                          scHpfParam->load());

    engine.setManualParameters (manualModeParam->get(),
                                thresholdParam->load(),
                                ratioParam->load(),
                                attackParam->load(),
                                releaseParam->load());

    // A state restore just landed: jump the smoothers to their new targets
    // instead of gliding, so preset recall is instant rather than a swoop.
    if (pendingSnap.exchange (false, std::memory_order_acquire))
    {
        engine.snapToTargets();
        bypassRamp.setCurrentAndTargetValue (bypassParam->get() ? 0.0f : 1.0f);
    }

    // Metering only - reading the magnitude does not touch the audio.
    inLevel.store (buffer.getMagnitude (0, numSamples), std::memory_order_relaxed);

    // Bypass needs a clean copy to crossfade against. The scratch buffer is
    // sized for the declared block length, so an oversized block is handled in
    // chunks rather than overrunning it - same reasoning as the engine.
    const int chansToCopy = juce::jmin (numOut, bypassDryBuffer.getNumChannels());

    // jmax(1) guards the stride: a zero here would spin forever if a host ever
    // called processBlock() before prepareToPlay().
    const int blockCap = juce::jmax (1, maxBlockSamples);

    for (int offset = 0; offset < numSamples; offset += blockCap)
    {
        const int chunk = juce::jmin (blockCap, numSamples - offset);

        for (int ch = 0; ch < chansToCopy; ++ch)
            bypassDryBuffer.copyFrom (ch, 0, buffer, ch, offset, chunk);

        juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), numOut, offset, chunk);
        engine.process (sub);

        bypassRamp.setTargetValue (bypassParam->get() ? 0.0f : 1.0f);

        if (bypassRamp.isSmoothing() || bypassRamp.getCurrentValue() < 1.0f)
        {
            for (int n = 0; n < chunk; ++n)
            {
                const float wetAmount = bypassRamp.getNextValue();

                for (int ch = 0; ch < chansToCopy; ++ch)
                {
                    const float processed = buffer.getSample (ch, offset + n);
                    const float clean     = bypassDryBuffer.getSample (ch, n);
                    buffer.setSample (ch, offset + n, clean + (processed - clean) * wetAmount);
                }
            }
        }
    }

    outLevel.store (buffer.getMagnitude (0, numSamples), std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* VictorRSwagVocalCompAudioProcessor::createEditor()
{
    return new VictorRSwagVocalCompAudioProcessorEditor (*this);
}

//==============================================================================
void VictorRSwagVocalCompAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VictorRSwagVocalCompAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            pendingSnap.store (true, std::memory_order_release);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VictorRSwagVocalCompAudioProcessor();
}
