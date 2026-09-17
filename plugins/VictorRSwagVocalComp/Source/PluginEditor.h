/*
  ==============================================================================

    PluginEditor.h - VictorRSwagVocalComp

    WebView editor. UI lives in Source/ui/public/index.html, embedded as
    binary data and served through the resource provider.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"
#include "ParameterIDs.hpp"

//==============================================================================
class VictorRSwagVocalCompAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit VictorRSwagVocalCompAudioProcessorEditor (VictorRSwagVocalCompAudioProcessor&);
    ~VictorRSwagVocalCompAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    VictorRSwagVocalCompAudioProcessor& audioProcessor;

    // ═══════════════════════════════════════════════════════════════════
    // CRITICAL: Member Declaration Order (Prevents DAW Crashes)
    //
    // C++ destroys members in REVERSE declaration order, and the WebView
    // holds references to the relays via .withOptionsFrom().
    //
    //   CORRECT ORDER:  Relays -> WebView -> Attachments
    //
    // Declaring webView before the relays would destroy the relays first and
    // leave the WebView touching freed memory on editor close.
    //
    // See: .agents/troubleshooting/resolutions/webview-member-order-crash.md
    // ═══════════════════════════════════════════════════════════════════

    // 1. PARAMETER RELAYS FIRST (no dependencies, destroyed last)
    juce::WebSliderRelay amountRelay    { ParameterIDs::amount    };
    juce::WebSliderRelay characterRelay { ParameterIDs::character };
    juce::WebSliderRelay mixRelay       { ParameterIDs::mix       };
    juce::WebSliderRelay outputRelay    { ParameterIDs::output    };
    juce::WebSliderRelay scHpfRelay     { ParameterIDs::scHpf     };
    juce::WebSliderRelay thresholdRelay { ParameterIDs::threshold };
    juce::WebSliderRelay ratioRelay     { ParameterIDs::ratio     };
    juce::WebSliderRelay attackRelay    { ParameterIDs::attack    };
    juce::WebSliderRelay releaseRelay   { ParameterIDs::release   };
    juce::WebToggleButtonRelay bypassRelay     { ParameterIDs::bypass     };
    juce::WebToggleButtonRelay manualModeRelay { ParameterIDs::manualMode };

    // 2. WEBVIEW SECOND (depends on relays, destroyed middle)
    std::unique_ptr<juce::WebBrowserComponent> webView;

    // 3. PARAMETER ATTACHMENTS LAST (depend on relays, destroyed first)
    std::unique_ptr<juce::WebSliderParameterAttachment> amountAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> characterAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> mixAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> outputAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> scHpfAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> thresholdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> ratioAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> attackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> releaseAttachment;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> bypassAttachment;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> manualModeAttachment;

    // Meter feed
    float lastSentOpto = -1.0f;
    float lastSentFet  = -1.0f;
    float lastSentIn   = -1.0f;
    float lastSentOut  = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VictorRSwagVocalCompAudioProcessorEditor)
};
