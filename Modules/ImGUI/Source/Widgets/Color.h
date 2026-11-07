#pragma once
#include <array>

// Colour temperature (UX-5): the sRGB colour of a blackbody at a temperature, normalised so its
// brightest channel is 1, and the way back from a colour to the temperature it sits at.
// Tanner Helland's fit to the CIE 1964 blackbody data (1000 K..40000 K, good to about one 8-bit
// step): 1900 K candle orange, 6500 K about white, 10000 K blue sky. Pure functions, no ImGui.
namespace MeWidgets::ColorTemperature
{
    constexpr double kMinKelvin = 1000.0;
    constexpr double kMaxKelvin = 12000.0;

    // sRGB 0..1, the brightest channel at 1.
    std::array<float, 3> ToSrgb( double InKelvin );

    // The temperature whose colour InSrgb is (after normalising it to a brightest channel of 1),
    // within InTolerance on every channel; false for colours off the blackbody curve (green,
    // magenta, black) or outside kMinKelvin..kMaxKelvin.
    bool FromSrgb( const std::array<float, 3>& InSrgb, double& OutKelvin, float InTolerance = 0.02f );

    // A named reference temperature for photographers' light (Candle 1900 K, Daylight 5600 K, ...).
    struct Preset
    {
        const char* Name;
        double Kelvin;
    };
    // In ascending temperature.
    const Preset* Presets( int& OutCount );
}
