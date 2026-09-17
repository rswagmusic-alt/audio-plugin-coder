/*
  ==============================================================================

    ParameterIDs.hpp - VictorRSwagVocalComp

    Single source of truth for APVTS parameter IDs.
    These strings MUST match .ideas/parameter-spec.md and the element IDs in
    Source/ui/public/index.html. They are frozen after the first release -
    saved presets and DAW sessions reference them.

  ==============================================================================
*/

#pragma once

namespace ParameterIDs
{
    static constexpr const char* amount    = "amount";
    static constexpr const char* character = "character";
    static constexpr const char* mix       = "mix";
    static constexpr const char* output    = "output";
    static constexpr const char* scHpf     = "sc_hpf";
    static constexpr const char* bypass    = "bypass";

    // Added for the MANUAL dynamics section. In MACRO mode these are inert and
    // amount/character drive the engine exactly as before.
    static constexpr const char* manualMode = "manual_mode";
    static constexpr const char* threshold  = "threshold";
    static constexpr const char* ratio      = "ratio";
    static constexpr const char* attack     = "attack";
    static constexpr const char* release    = "release";
}
