/*
  ==============================================================================

    PluginEditor.cpp - VictorRSwagVocalComp

  ==============================================================================
*/

#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    constexpr int   kEditorWidth   = 820;   // matches Design/v2-test.html
    constexpr int   kEditorHeight  = 540;
    constexpr int   kMeterHz       = 30;
    constexpr float kMeterEpsilon  = 0.02f; // dB     - skip sends below this delta
    constexpr float kLevelEpsilon  = 0.004f;// linear - same idea for IN/OUT

    const char* getMimeForExtension (const juce::String& extension)
    {
        static const std::unordered_map<juce::String, const char*> mimeMap
        {
            { { "htm"  }, "text/html"       },
            { { "html" }, "text/html"       },
            { { "css"  }, "text/css"        },
            { { "js"   }, "text/javascript" },
            { { "json" }, "application/json"},
            { { "svg"  }, "image/svg+xml"   },
            { { "png"  }, "image/png"       },
            { { "jpg"  }, "image/jpeg"      },
            { { "jpeg" }, "image/jpeg"      },
            { { "webp" }, "image/webp"      },
            { { "woff2"}, "font/woff2"      }
        };

        if (const auto it = mimeMap.find (extension.toLowerCase()); it != mimeMap.end())
            return it->second;

        return "application/octet-stream";
    }
}

//==============================================================================
VictorRSwagVocalCompAudioProcessorEditor::VictorRSwagVocalCompAudioProcessorEditor (
    VictorRSwagVocalCompAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // ═══════════════════════════════════════════════════════════════════
    // MANDATORY CONSTRUCTOR ORDER - see known-issues.yaml webview-010
    //   1. attachments   2. WebView   3. addAndMakeVisible
    //   4. goToURL       5. setSize LAST
    //
    // Getting this wrong produces a black plugin window: the resource
    // provider callback is simply never invoked, with no error anywhere.
    // Declaration order in the header is a separate concern (relays ->
    // WebView -> attachments) and governs DESTRUCTION, not construction.
    // ═══════════════════════════════════════════════════════════════════

    // --- 1. ATTACHMENTS FIRST -------------------------------------------
    // These bind parameter <-> relay. They do not touch the WebView, so they
    // can and must be built before it exists.
    auto& apvts = audioProcessor.getAPVTS();

    auto attachSlider = [&apvts] (const char* id, juce::WebSliderRelay& relay)
    {
        auto* param = apvts.getParameter (id);
        jassert (param != nullptr);
        return std::make_unique<juce::WebSliderParameterAttachment> (*param, relay, nullptr);
    };

    amountAttachment    = attachSlider (ParameterIDs::amount,    amountRelay);
    characterAttachment = attachSlider (ParameterIDs::character, characterRelay);
    mixAttachment       = attachSlider (ParameterIDs::mix,       mixRelay);
    outputAttachment    = attachSlider (ParameterIDs::output,    outputRelay);
    scHpfAttachment     = attachSlider (ParameterIDs::scHpf,     scHpfRelay);
    thresholdAttachment = attachSlider (ParameterIDs::threshold, thresholdRelay);
    ratioAttachment     = attachSlider (ParameterIDs::ratio,     ratioRelay);
    attackAttachment    = attachSlider (ParameterIDs::attack,    attackRelay);
    releaseAttachment   = attachSlider (ParameterIDs::release,   releaseRelay);

    if (auto* bypass = apvts.getParameter (ParameterIDs::bypass))
        bypassAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment> (
            *bypass, bypassRelay, nullptr);

    if (auto* manual = apvts.getParameter (ParameterIDs::manualMode))
        manualModeAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment> (
            *manual, manualModeRelay, nullptr);

    // --- 2. WEBVIEW ------------------------------------------------------
    auto options = juce::WebBrowserComponent::Options {}
                       .withNativeIntegrationEnabled()
                       .withResourceProvider ([this] (const auto& url) { return getResource (url); })
                       .withOptionsFrom (amountRelay)
                       .withOptionsFrom (characterRelay)
                       .withOptionsFrom (mixRelay)
                       .withOptionsFrom (outputRelay)
                       .withOptionsFrom (scHpfRelay)
                       .withOptionsFrom (thresholdRelay)
                       .withOptionsFrom (ratioRelay)
                       .withOptionsFrom (attackRelay)
                       .withOptionsFrom (releaseRelay)
                       .withOptionsFrom (bypassRelay)
                       .withOptionsFrom (manualModeRelay);

   #if JUCE_WINDOWS
    // Windows needs the WebView2 backend named explicitly plus a writable user
    // data folder. macOS uses system WKWebView and Linux uses WebKitGTK, where
    // these options do not apply - see .agents/rules/juce-build-protocols.md.
    options = options
                  .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                  .withWinWebView2Options (
                      juce::WebBrowserComponent::Options::WinWebView2 {}
                          .withUserDataFolder (juce::File::getSpecialLocation (
                              juce::File::SpecialLocationType::tempDirectory)));
   #endif

    webView = std::make_unique<juce::WebBrowserComponent> (std::move (options));

    // --- 3. ADD TO COMPONENT (after attachments exist) -------------------
    addAndMakeVisible (*webView);

    // --- 4. LOAD CONTENT via the resource provider, never a data: URI ----
    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    // --- 5. SIZE LAST ----------------------------------------------------
    setResizable (false, false);
    setSize (kEditorWidth, kEditorHeight);

    startTimerHz (kMeterHz);
}

VictorRSwagVocalCompAudioProcessorEditor::~VictorRSwagVocalCompAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void VictorRSwagVocalCompAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Matches the panel mid-tone in Design/v2-head.part, so any frame drawn
    // before the WebView paints is the right colour rather than a flash of grey.
    g.fillAll (juce::Colour (0xff1A1E24));
}

void VictorRSwagVocalCompAudioProcessorEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
}

//==============================================================================
void VictorRSwagVocalCompAudioProcessorEditor::timerCallback()
{
    const float opto = audioProcessor.getOptoGrDb();
    const float fet  = audioProcessor.getFetGrDb();
    const float lin  = audioProcessor.getInLevel();
    const float lout = audioProcessor.getOutLevel();

    // Don't wake the WebView when nothing moved.
    if (std::abs (opto - lastSentOpto) < kMeterEpsilon
        && std::abs (fet  - lastSentFet)  < kMeterEpsilon
        && std::abs (lin  - lastSentIn)   < kLevelEpsilon
        && std::abs (lout - lastSentOut)  < kLevelEpsilon)
        return;

    lastSentOpto = opto;
    lastSentFet  = fet;
    lastSentIn   = lin;
    lastSentOut  = lout;

    auto payload = new juce::DynamicObject();
    payload->setProperty ("opto", opto);
    payload->setProperty ("fet",  fet);
    payload->setProperty ("lin",  lin);
    payload->setProperty ("lout", lout);

    if (webView != nullptr)
        webView->emitEventIfBrowserIsVisible ("grUpdate", juce::var { payload });
}

//==============================================================================
std::optional<juce::WebBrowserComponent::Resource>
VictorRSwagVocalCompAudioProcessorEditor::getResource (const juce::String& url)
{
    const auto resourceName = url == "/" ? juce::String { "index.html" }
                                         : url.fromFirstOccurrenceOf ("/", false, false);

    int dataSize = 0;
    const char* data = nullptr;

    // Only index.html ships. test-local.html is a browser harness and is
    // deliberately not embedded.
    if (resourceName == "index.html")
    {
        data     = BinaryData::index_html;
        dataSize = BinaryData::index_htmlSize;
    }

    if (data == nullptr || dataSize <= 0)
        return std::nullopt;

    std::vector<std::byte> bytes (static_cast<size_t> (dataSize));
    std::memcpy (bytes.data(), data, static_cast<size_t> (dataSize));

    const auto extension = resourceName.fromLastOccurrenceOf (".", false, false);

    return juce::WebBrowserComponent::Resource {
        std::move (bytes), juce::String { getMimeForExtension (extension) }
    };
}
