#pragma once
#include "Reflection/Reflection.h"
#include <string>
#include <string_view>

// What a number means, for people (UX-2): formatting with a unit ("2.5 m", "90°", "5000 K",
// "-6.0 dB", "50%", "f/2.8"), typed entry that understands units and simple arithmetic
// ("150 cm", "90°", "0.5 turn", "-6 dB", "2*3 m", "(1+2)/4"), and log mapping for log scrubs.
// Pure functions: no ImGui, so they unit test anywhere.
namespace MeWidgets
{
    using Reflection::Unit;

    struct UnitInfo
    {
        const char* Name;       // "Meters"
        const char* Symbol;     // "m"; shown after the number (or before it, for f-stops)
        const char* Quantity;   // "a length"; error messages say what the field wants
    };
    const UnitInfo& GetUnitInfo( Unit InUnit );

    // Stored value <-> the number shown (degrees for Radians, dB for LinearGain, percent for
    // Percent, semitones for PitchRatio; the same number for every other unit).
    double ToDisplay( Unit InUnit, double InStored );
    double FromDisplay( Unit InUnit, double InDisplay );

    // A number with about four significant digits, trailing zeros trimmed: 12.35, 0.025, 1500.
    // InDecimals >= 0 fixes the decimals instead.
    std::string FormatNumber( double InValue, int InDecimals = -1 );
    // The stored value shown in its unit: "2.5 m", "90°", "-6.0 dB", "+1.5 EV", "f/2.8".
    std::string FormatValue( Unit InUnit, double InStored, int InDecimals = -1 );

    // Typed entry: a number or an expression (+ - * / and parentheses), each number optionally
    // followed by a unit of the field's kind ("150 cm" into metres, "0.5 turn" into degrees).
    // Returns the stored value, or false with OutError saying why.
    bool ParseValue( Unit InUnit, std::string_view InText, double& OutStored, std::string* OutError = nullptr );

    // Log mapping between a value in [InMin, InMax] and t in [0, 1]. A range starting at zero (or
    // below) maps through a shifted log, so 0 is reachable and the low end still gets room.
    namespace LogMap
    {
        double ToNormalized( double InValue, double InMin, double InMax );
        double FromNormalized( double InT, double InMin, double InMax );
    }

    // A round step for a span ("Alt snaps"): 1, 2 or 5 times a power of ten, about a tenth of it.
    double NiceStep( double InSpan );
}
