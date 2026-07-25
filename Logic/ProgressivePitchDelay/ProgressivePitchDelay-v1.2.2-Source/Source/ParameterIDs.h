#pragma once

#include <JuceHeader.h>

namespace ParameterIDs
{
    inline constexpr auto delayMode      = "delayMode";
    inline constexpr auto pitchDirection = "pitchDirection";
    inline constexpr auto semitones      = "semitones";
    inline constexpr auto repeatCount    = "repeatCount";
    inline constexpr auto delayTime      = "delayTime";
    inline constexpr auto tempoSync      = "tempoSync";
    inline constexpr auto syncDivision   = "syncDivision";
    inline constexpr auto dryDb          = "dryDb";
    inline constexpr auto firstRepeatDb  = "firstRepeatDb";
    inline constexpr auto decayDb        = "decayDb";
    inline constexpr auto headroom       = "headroom";
    inline constexpr auto quality        = "quality";
}

namespace ParameterChoices
{
    inline const juce::StringArray delayModes      { "Normal", "Reversed" };
    inline const juce::StringArray pitchDirections { "Lower", "Higher" };
    inline const juce::StringArray qualities       { "Draft", "High" };
    inline const juce::StringArray syncDivisions
    {
        "1/64", "1/32T", "1/32", "1/32D",
        "1/16T", "1/16", "1/16D",
        "1/8T", "1/8", "1/8D",
        "1/4T", "1/4", "1/4D",
        "1/2T", "1/2", "1/2D", "1/1"
    };

    // Length in quarter-note beats.
    inline constexpr std::array<double, 17> syncQuarterNotes
    {
        1.0 / 16.0, 1.0 / 12.0, 1.0 / 8.0, 3.0 / 16.0,
        1.0 / 6.0,  1.0 / 4.0,  3.0 / 8.0,
        1.0 / 3.0,  1.0 / 2.0,  3.0 / 4.0,
        2.0 / 3.0,  1.0,        1.5,
        4.0 / 3.0,  2.0,        3.0, 4.0
    };
}
