# VictorRSwagVocalComp - "Victor R Swag Vocal Compressor"
# Hybrid opto -> FET vocal compressor, WebView UI.
# Cross-platform: Windows (VST3), macOS (VST3/AU), Linux (VST3/LV2)

# ============================================
# PLATFORM-SPECIFIC CONFIGURATION
# ============================================
# JUCE 9 has TWO separate WebView flags - see .agents/rules/juce-build-protocols.md
#   Windows: NEEDS_WEBVIEW2   (static-links the WebView2 SDK)
#   Linux:   NEEDS_WEB_BROWSER (links webkit2gtk + GTK, adds include paths)
#   macOS:   neither - WKWebView is a system framework
set(NEEDS_WEBVIEW2 FALSE)
set(NEEDS_WEB_BROWSER FALSE)
set(AU_MAIN_TYPE "")
set(LV2_URI "")

if(WIN32)
    set(PLUGIN_FORMATS VST3 Standalone)
    set(NEEDS_WEBVIEW2 TRUE)
    set(WEBVIEW_BACKEND "WebView2")
elseif(APPLE)
    set(PLUGIN_FORMATS VST3 AU Standalone)
    set(WEBVIEW_BACKEND "WKWebView")
    set(AU_MAIN_TYPE kAudioUnitType_Effect)
elseif(UNIX)
    set(PLUGIN_FORMATS VST3 LV2 Standalone)
    set(NEEDS_WEB_BROWSER TRUE)
    set(WEBVIEW_BACKEND "WebKitGTK")
    set(LV2_URI "https://github.com/noizefield/audio-plugin-coder/VictorRSwagVocalComp")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

message(STATUS "VictorRSwagVocalComp: Building for ${PLUGIN_FORMATS}")
message(STATUS "VictorRSwagVocalComp: WebView backend: ${WEBVIEW_BACKEND}")

# ============================================
# BINARY DATA (Web UI Resources)
# ============================================
# webview-008: ALL JavaScript is inline in index.html - no .js files here.
# test-local.html is a browser test harness and is deliberately NOT embedded.
juce_add_binary_data(VictorRSwagVocalComp_WebUI
    SOURCES
        Source/ui/public/index.html
)

# ============================================
# PLUGIN DEFINITION
# ============================================
juce_add_plugin(VictorRSwagVocalComp
    COMPANY_NAME "Victor R Swag"
    PLUGIN_MANUFACTURER_CODE Vrsw
    PLUGIN_CODE Vrvc
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "VRS Vocal Comp"
    NEEDS_WEBVIEW2 ${NEEDS_WEBVIEW2}
    NEEDS_WEB_BROWSER ${NEEDS_WEB_BROWSER}
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE ${AU_MAIN_TYPE}
    LV2URI ${LV2_URI}
    COPY_PLUGIN_AFTER_BUILD FALSE
)

# ============================================
# SOURCE FILES
# ============================================
target_sources(VictorRSwagVocalComp
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(VictorRSwagVocalComp
    PRIVATE
        Source
)

# ============================================
# LINK LIBRARIES
# ============================================
target_link_libraries(VictorRSwagVocalComp
    PRIVATE
        VictorRSwagVocalComp_WebUI
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ============================================
# COMPILE DEFINITIONS
# ============================================
target_compile_definitions(VictorRSwagVocalComp
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        _USE_MATH_DEFINES=1
)

if(WIN32)
    target_compile_definitions(VictorRSwagVocalComp
        PUBLIC
            JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    )
endif()

if(APPLE)
    target_compile_definitions(VictorRSwagVocalComp
        PUBLIC
            JUCE_USE_CURL=0
    )
endif()

if(UNIX AND NOT APPLE)
    target_compile_definitions(VictorRSwagVocalComp
        PUBLIC
            JUCE_USE_CURL=0
            JUCE_JACK=1
    )
endif()

# macOS universal binary is handled globally via CMAKE_OSX_ARCHITECTURES at
# configure time. Per-target XCODE_ATTRIBUTE_ARCHS is FORBIDDEN - it conflicts
# with the global setting and breaks the link step.
